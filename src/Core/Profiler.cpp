#include "Emerald/Core/Profiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "Emerald/Core/Log.h"
#include "Emerald/Math/Mat4.h"
#include "Emerald/Renderer/Renderer2D.h"

#include "DebugFont.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

namespace Emerald {

namespace {

constexpr usize kMaxTraceEvents = 100000;

struct OpenScope {
    const char* Name = "";
    u64 StartNs = 0;
    u64 ChildNs = 0;
    u16 Depth = 0;
};

struct CompletedScope {
    const char* Name = "";
    u64 StartNs = 0;
    u64 DurationNs = 0;
    u64 SelfNs = 0;
    u16 Depth = 0;
};

struct ThreadRecord {
    std::mutex Mutex;
    std::vector<CompletedScope> Completed;
    // Open scopes stay here, not in thread-local storage. The owning thread is the only writer.
    std::vector<OpenScope> Stack;
    std::thread::id Id{};
    u32 Tid = 0;
};

enum class TraceKind : u8 { Scope, Frame, Gpu };

struct TraceEvent {
    const char* Name = "";
    u64 StartNs = 0;
    u64 DurationNs = 0;
    u32 Tid = 1;
    u16 Depth = 0;
    TraceKind Kind = TraceKind::Scope;
    u32 DrawCalls = 0;
    u32 Lines = 0;
    u32 Sprites = 0;
    usize ArenaBytes = 0;
    usize PoolBytes = 0;
};

// C++ thread_local, even a trivially destructible pointer, still deadlocks std::thread::join in
// MSVC debug and aborts with 0xc0000409 in release (ProfileTests). One Win32 fiber-local slot, or
// one pthread key, with a null destructor: nothing runs when the thread exits.
#if defined(_WIN32)
DWORD RecordSlot()
{
    static const DWORD slot = FlsAlloc(nullptr);
    return slot;
}
#elif defined(__linux__) || defined(__APPLE__)
pthread_key_t RecordKey()
{
    static const pthread_key_t key = []() -> pthread_key_t {
        pthread_key_t created{};
        if (pthread_key_create(&created, nullptr) != 0)
            return {};
        return created;
    }();
    return key;
}
#endif

ThreadRecord* LoadRecord()
{
#if defined(_WIN32)
    return static_cast<ThreadRecord*>(FlsGetValue(RecordSlot()));
#elif defined(__linux__) || defined(__APPLE__)
    return static_cast<ThreadRecord*>(pthread_getspecific(RecordKey()));
#else
    return nullptr;
#endif
}

void StoreRecord(ThreadRecord* record)
{
#if defined(_WIN32)
    FlsSetValue(RecordSlot(), record);
#elif defined(__linux__) || defined(__APPLE__)
    pthread_setspecific(RecordKey(), record);
#else
    (void)record;
#endif
}

void AppendUnsigned(std::string& out, unsigned long long value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", value);
    out += buf;
}

// Microseconds with 3 decimal places, from nanoseconds. Integer so a test clock round-trips.
void AppendMicroseconds(std::string& out, u64 nanoseconds)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%llu.%03llu",
                  static_cast<unsigned long long>(nanoseconds / 1000),
                  static_cast<unsigned long long>(nanoseconds % 1000));
    out += buf;
}

void AppendEscaped(std::string& out, const char* text)
{
    out.push_back('"');
    if (!text)
        text = "";
    for (const char* p = text; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

} // namespace

struct Profiler::State {
    ThreadRecord* LocalThread();

    std::mutex Mutex;
    std::vector<std::unique_ptr<ThreadRecord>> Threads;
    std::vector<TraceEvent> Trace;
    std::atomic<bool> FrameOpen{false};
    u64 FrameStart = 0;
    u64 OriginNs = 0;
    bool HasOrigin = false;
    u32 NextTid = 1;
    std::thread::id MainThread{};
    bool HaveMain = false;
    u64 (*Clock)() = nullptr;
    f64 GpuMs = 0;
    bool GpuValid = false;
    bool GpuFresh = false;
};

ThreadRecord* Profiler::State::LocalThread()
{
    if (ThreadRecord* existing = LoadRecord())
        return existing;
    auto record = std::make_unique<ThreadRecord>();
    record->Id = std::this_thread::get_id();
    record->Completed.reserve(32);
    record->Stack.reserve(8);
    std::lock_guard lock(Mutex);
    record->Tid = NextTid++;
    Threads.push_back(std::move(record));
    ThreadRecord* stored = Threads.back().get();
    StoreRecord(stored);
    return stored;
}

Profiler::Profiler() : m_State(std::make_unique<State>())
{
}

Profiler::~Profiler() = default;

Profiler& Profiler::Get()
{
    static Profiler instance;
    return instance;
}

void Profiler::SetEnabled(bool enabled)
{
    ProfilerRecordingFlag().store(enabled, std::memory_order_relaxed);
}

bool Profiler::IsEnabled() const
{
    return ProfilerRecording();
}

void Profiler::SetOverlayVisible(bool visible)
{
    m_OverlayVisible = visible;
    if (visible)
        SetEnabled(true);
    else if (m_TracePath.empty())
        SetEnabled(false);
}

void Profiler::SetTracePath(std::filesystem::path path)
{
    m_TracePath = std::move(path);
    if (!m_TracePath.empty())
        SetEnabled(true);
}

void Profiler::SetClock(u64 (*clock)())
{
    m_State->Clock = clock;
}

u64 Profiler::Now() const
{
    if (m_State->Clock)
        return m_State->Clock();
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

void Profiler::BeginFrame()
{
    if (!IsEnabled())
        return;
    State& state = *m_State;
    state.FrameStart = Now();
    if (!state.HasOrigin) {
        state.OriginNs = state.FrameStart;
        state.HasOrigin = true;
    }
    if (!state.HaveMain) {
        state.MainThread = std::this_thread::get_id();
        state.HaveMain = true;
    }
    state.FrameOpen.store(true, std::memory_order_release);
}

bool Profiler::BeginScope(const char* name)
{
    // FrameOpen is the second half of the disabled path: recording can be on and still skip work
    // that happens outside the frame (startup). Neither check takes the profiler lock.
    if (!ProfilerRecording() || !m_State->FrameOpen.load(std::memory_order_acquire))
        return false;
    ThreadRecord* record = m_State->LocalThread();
    OpenScope scope;
    scope.Name = name != nullptr && name[0] != '\0' ? name : "?";
    scope.StartNs = Now();
    scope.ChildNs = 0;
    scope.Depth = static_cast<u16>(record->Stack.size() > 65535 ? 65535 : record->Stack.size());
    record->Stack.push_back(scope);
    return true;
}

void Profiler::EndScope()
{
    ThreadRecord* record = LoadRecord();
    if (!record || record->Stack.empty())
        return;
    const OpenScope scope = record->Stack.back();
    record->Stack.pop_back();
    const u64 end = Now();
    const u64 duration = end >= scope.StartNs ? end - scope.StartNs : 0;
    if (!record->Stack.empty())
        record->Stack.back().ChildNs += duration;
    CompletedScope done;
    done.Name = scope.Name;
    done.StartNs = scope.StartNs;
    done.DurationNs = duration;
    done.SelfNs = duration >= scope.ChildNs ? duration - scope.ChildNs : 0;
    done.Depth = scope.Depth;
    std::lock_guard lock(record->Mutex);
    record->Completed.push_back(done);
}

void Profiler::ReportGpuTime(f64 milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 0.0)
        return;
    m_State->GpuMs = milliseconds;
    m_State->GpuValid = true;
    m_State->GpuFresh = true;
}

void Profiler::EndFrame(const ProfileFrameStats& stats)
{
    State& state = *m_State;
    if (!state.FrameOpen.load(std::memory_order_relaxed))
        return;
    const u64 end = Now();
    state.FrameOpen.store(false, std::memory_order_release);
    const u64 frameNs = end >= state.FrameStart ? end - state.FrameStart : 0;

    struct Bag {
        u32 Tid = 1;
        std::thread::id Id{};
        std::vector<CompletedScope> Scopes;
    };
    std::vector<Bag> bags;
    u32 mainTid = 1;
    {
        std::lock_guard lock(state.Mutex);
        for (const std::unique_ptr<ThreadRecord>& thread : state.Threads) {
            if (thread->Id == state.MainThread)
                mainTid = thread->Tid;
            Bag bag;
            bag.Tid = thread->Tid;
            bag.Id = thread->Id;
            std::lock_guard guard(thread->Mutex);
            bag.Scopes.swap(thread->Completed);
            if (!bag.Scopes.empty())
                bags.push_back(std::move(bag));
        }
    }

    struct Acc {
        const char* Name = "";
        u64 Total = 0;
        u64 Self = 0;
        u32 Calls = 0;
    };
    std::vector<Acc> acc;
    for (const Bag& bag : bags) {
        if (bag.Id != state.MainThread)
            continue;
        for (const CompletedScope& scope : bag.Scopes) {
            Acc* slot = nullptr;
            for (Acc& item : acc) {
                // Compare the text. MSVC debug does not pool string literals, so two "A" scopes
                // are different pointers and would otherwise stay split. Release does pool them.
                const bool same = item.Name == scope.Name ||
                                  (item.Name != nullptr && scope.Name != nullptr &&
                                   std::strcmp(item.Name, scope.Name) == 0);
                if (same) {
                    slot = &item;
                    break;
                }
            }
            if (!slot) {
                acc.push_back(Acc{scope.Name});
                slot = &acc.back();
            }
            slot->Total += scope.DurationNs;
            slot->Self += scope.SelfNs;
            ++slot->Calls;
        }
    }
    std::sort(acc.begin(), acc.end(), [](const Acc& a, const Acc& b) {
        if (a.Self != b.Self)
            return a.Self > b.Self;
        return std::strcmp(a.Name, b.Name) < 0;
    });

    const u32 top =
        static_cast<u32>(acc.size() < kProfileTopScopes ? acc.size() : kProfileTopScopes);
    m_Snapshot.TopCount = top;
    for (u32 i = 0; i < top; ++i) {
        m_Snapshot.Top[i].Name = acc[i].Name;
        m_Snapshot.Top[i].TotalMs = static_cast<f64>(acc[i].Total) / 1e6;
        m_Snapshot.Top[i].SelfMs = static_cast<f64>(acc[i].Self) / 1e6;
        m_Snapshot.Top[i].Calls = acc[i].Calls;
    }
    for (u32 i = top; i < kProfileTopScopes; ++i)
        m_Snapshot.Top[i] = {};

    const f32 frameMs = static_cast<f32>(static_cast<f64>(frameNs) / 1e6);
    if (m_Snapshot.FrameCount < kProfileGraphSamples) {
        m_Snapshot.FrameMsGraph[m_Snapshot.FrameCount++] = frameMs;
    } else {
        auto& graph = m_Snapshot.FrameMsGraph;
        for (u32 i = 1; i < kProfileGraphSamples; ++i)
            graph[i - 1] = graph[i];
        graph.back() = frameMs;
    }
    f64 sum = 0;
    for (u32 i = 0; i < m_Snapshot.FrameCount; ++i)
        sum += m_Snapshot.FrameMsGraph[i];
    m_Snapshot.AverageMs = sum / static_cast<f64>(m_Snapshot.FrameCount);
    m_Snapshot.FrameMs = static_cast<f64>(frameNs) / 1e6;
    m_Snapshot.GpuMs = state.GpuMs;
    m_Snapshot.GpuValid = state.GpuValid;
    m_Snapshot.DrawCalls = stats.DrawCalls;
    m_Snapshot.Lines = stats.Lines;
    m_Snapshot.Sprites = stats.Sprites;
    m_Snapshot.ArenaBytes = stats.ArenaBytes;
    m_Snapshot.ArenaCapacity = stats.ArenaCapacity;
    m_Snapshot.ArenaPeakBytes = stats.ArenaPeakBytes;
    m_Snapshot.ArenaOverflowFrames = stats.ArenaOverflowFrames;
    m_Snapshot.PoolBytes = stats.PoolBytes;
    m_Snapshot.PoolPeakBytes = stats.PoolPeakBytes;

    std::vector<TraceEvent> added;
    added.reserve(16);
    for (const Bag& bag : bags) {
        for (const CompletedScope& scope : bag.Scopes) {
            TraceEvent event;
            event.Name = scope.Name;
            event.StartNs = scope.StartNs;
            event.DurationNs = scope.DurationNs;
            event.Tid = bag.Tid;
            event.Depth = scope.Depth;
            event.Kind = TraceKind::Scope;
            added.push_back(event);
        }
    }
    TraceEvent frame;
    frame.Name = "frame";
    frame.StartNs = state.FrameStart;
    frame.DurationNs = frameNs;
    frame.Tid = mainTid;
    frame.Kind = TraceKind::Frame;
    frame.DrawCalls = stats.DrawCalls;
    frame.Lines = stats.Lines;
    frame.Sprites = stats.Sprites;
    frame.ArenaBytes = stats.ArenaBytes;
    frame.PoolBytes = stats.PoolBytes;
    added.push_back(frame);
    if (state.GpuFresh && state.GpuValid) {
        TraceEvent gpu;
        gpu.Name = "GPU";
        gpu.StartNs = state.FrameStart;
        const f64 ns = state.GpuMs * 1'000'000.0;
        gpu.DurationNs = ns > 0.0 ? static_cast<u64>(std::llround(ns)) : 0;
        gpu.Tid = mainTid;
        gpu.Kind = TraceKind::Gpu;
        added.push_back(gpu);
        state.GpuFresh = false;
    }

    std::lock_guard lock(state.Mutex);
    state.Trace.insert(state.Trace.end(), added.begin(), added.end());
    if (state.Trace.size() > kMaxTraceEvents) {
        const usize drop = state.Trace.size() / 2;
        state.Trace.erase(state.Trace.begin(),
                          state.Trace.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

void Profiler::Clear()
{
    std::lock_guard lock(m_State->Mutex);
    for (std::unique_ptr<ThreadRecord>& thread : m_State->Threads) {
        std::lock_guard guard(thread->Mutex);
        thread->Completed.clear();
    }
    m_State->Trace.clear();
    m_State->FrameOpen.store(false, std::memory_order_relaxed);
    m_State->HasOrigin = false;
    m_State->OriginNs = 0;
    m_State->GpuValid = false;
    m_State->GpuFresh = false;
    m_State->GpuMs = 0;
    m_State->HaveMain = false;
    m_Snapshot = {};
    if (ThreadRecord* record = LoadRecord())
        record->Stack.clear();
}

std::string Profiler::ExportChromeTrace() const
{
    // unique_ptr's const operator* still yields a mutable State, which is what the lock needs.
    State& state = *m_State;
    std::lock_guard lock(state.Mutex);
    std::string out;
    out.reserve(state.Trace.size() * 96 + 32);
    out += "{\"traceEvents\":[";
    bool first = true;
    for (const TraceEvent& event : state.Trace) {
        if (!first)
            out += ',';
        first = false;
        out += "\n{\"name\":";
        AppendEscaped(out, event.Name);
        out += ",\"cat\":";
        switch (event.Kind) {
        case TraceKind::Scope:
            out += "\"cpu\"";
            break;
        case TraceKind::Frame:
            out += "\"frame\"";
            break;
        case TraceKind::Gpu:
            out += "\"gpu\"";
            break;
        }
        out += ",\"ph\":\"X\",\"ts\":";
        const u64 start = event.StartNs >= state.OriginNs ? event.StartNs - state.OriginNs : 0;
        AppendMicroseconds(out, start);
        out += ",\"dur\":";
        AppendMicroseconds(out, event.DurationNs);
        out += ",\"pid\":1,\"tid\":";
        AppendUnsigned(out, event.Tid);
        if (event.Kind == TraceKind::Scope) {
            out += ",\"args\":{\"depth\":";
            AppendUnsigned(out, event.Depth);
            out += '}';
        } else if (event.Kind == TraceKind::Frame) {
            out += ",\"args\":{\"drawCalls\":";
            AppendUnsigned(out, event.DrawCalls);
            out += ",\"lines\":";
            AppendUnsigned(out, event.Lines);
            out += ",\"sprites\":";
            AppendUnsigned(out, event.Sprites);
            out += ",\"arena\":";
            AppendUnsigned(out, event.ArenaBytes);
            out += ",\"pool\":";
            AppendUnsigned(out, event.PoolBytes);
            out += '}';
        }
        out += '}';
    }
    if (!state.Trace.empty())
        out += '\n';
    out += "]}\n";
    return out;
}

bool Profiler::SaveChromeTrace(const std::filesystem::path& path) const
{
    if (path.empty())
        return false;
    std::error_code error;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        EM_CORE_ERROR("Profiler: cannot write {}", path.string());
        return false;
    }
    const std::string json = ExportChromeTrace();
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!out) {
        EM_CORE_ERROR("Profiler: failed while writing {}", path.string());
        return false;
    }
    return true;
}

void Profiler::DrawImGui()
{
    if (!m_OverlayVisible)
        return;
#if EMERALD_WITH_IMGUI
    ImGui::SetNextWindowPos(ImVec2(20.0f, 80.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Profiler", &open, ImGuiWindowFlags_NoNavInputs)) {
        ImGui::End();
        if (!open)
            SetOverlayVisible(false);
        return;
    }
    if (!open)
        SetOverlayVisible(false);

    const ProfileSnapshot& snap = m_Snapshot;
    if (snap.FrameCount == 0) {
        ImGui::TextUnformatted("No frames yet");
    } else {
        const double fps = snap.FrameMs > 0.0 ? 1000.0 / snap.FrameMs : 0.0;
        ImGui::Text("Frame %.2f ms (%.0f fps)   avg %.2f", snap.FrameMs, fps, snap.AverageMs);
    }
    if (snap.GpuValid)
        ImGui::Text("GPU %.2f ms", snap.GpuMs);
    else
        ImGui::TextUnformatted("GPU n/a");

    if (snap.FrameCount > 0) {
        float maxMs = 33.3f;
        for (u32 i = 0; i < snap.FrameCount; ++i)
            maxMs = std::max(maxMs, snap.FrameMsGraph[i]);
        ImGui::PlotLines("##frame", snap.FrameMsGraph.data(), static_cast<int>(snap.FrameCount), 0,
                         nullptr, 0.0f, maxMs, ImVec2(-1.0f, 72.0f));
    }
    for (u32 i = 0; i < snap.TopCount; ++i) {
        const ProfileScopeStat& scope = snap.Top[i];
        ImGui::Text("%s   %.3f ms   self %.3f   x%u", scope.Name, scope.TotalMs, scope.SelfMs,
                    scope.Calls);
    }
    ImGui::Separator();
    ImGui::Text("Draw calls %u    lines %u    sprites %u", snap.DrawCalls, snap.Lines,
                snap.Sprites);
    ImGui::Text("Arena %llu / %llu KiB   peak %llu   overflow %llu",
                static_cast<unsigned long long>(snap.ArenaBytes / 1024),
                static_cast<unsigned long long>(snap.ArenaCapacity / 1024),
                static_cast<unsigned long long>(snap.ArenaPeakBytes / 1024),
                static_cast<unsigned long long>(snap.ArenaOverflowFrames));
    ImGui::Text("Pools %llu KiB in use (peak %llu)",
                static_cast<unsigned long long>(snap.PoolBytes / 1024),
                static_cast<unsigned long long>(snap.PoolPeakBytes / 1024));
    ImGui::TextDisabled("F3 hides this. F4 writes a Chrome trace.");
    ImGui::End();
#endif
}

void Profiler::DrawOverlay(Renderer2D& renderer, f32 pixelWidth, f32 pixelHeight) const
{
    if (!m_OverlayVisible || pixelWidth < 1.0f || pixelHeight < 1.0f)
        return;

    const ProfileSnapshot& snap = m_Snapshot;
    const f32 scale = pixelWidth < 720.0f ? 1.0f : 2.0f;
    const f32 line = DebugTextLineHeight(scale);
    const f32 margin = 10.0f;
    const f32 graphH = 28.0f * scale;
    const f32 panelW = std::min(pixelWidth - margin * 2.0f, 52.0f * 6.0f * scale);
    const f32 panelH =
        8.0f + line * 2.0f + 4.0f + graphH + 6.0f + line * static_cast<f32>(snap.TopCount + 4);

    // The panel is UI: a lit world would shade it with the scene's lights.
    const bool wasLit = renderer.IsLightingEnabled();
    renderer.SetLightingEnabled(false);
    renderer.Begin(Mat4::OrthoPixelSpace(pixelWidth, pixelHeight));
    renderer.FillRect({margin, margin}, {panelW, panelH}, {0.03f, 0.07f, 0.05f, 0.90f});

    char buf[160];
    f32 y = margin + 6.0f;
    const f32 x = margin + 8.0f;
    const Vec4 text{0.78f, 0.95f, 0.84f, 1.0f};
    const Vec4 dim{0.55f, 0.72f, 0.62f, 1.0f};

    if (snap.FrameCount == 0) {
        DrawDebugText(renderer, {x, y}, "Profiler (no frames yet)", text, scale);
    } else {
        const f64 fps = snap.FrameMs > 0.0 ? 1000.0 / snap.FrameMs : 0.0;
        std::snprintf(buf, sizeof(buf), "Profiler %.2f ms (%.0f fps)", snap.FrameMs, fps);
        DrawDebugText(renderer, {x, y}, buf, text, scale);
    }
    y += line;
    if (snap.GpuValid)
        std::snprintf(buf, sizeof(buf), "GPU %.2f ms", snap.GpuMs);
    else
        std::snprintf(buf, sizeof(buf), "GPU n/a");
    DrawDebugText(renderer, {x, y}, buf, dim, scale);
    y += line + 2.0f;

    const f32 graphW = std::max(panelW - 16.0f, 1.0f);
    renderer.FillRect({x, y}, {graphW, graphH}, {0.01f, 0.03f, 0.02f, 1.0f});
    f32 maxMs = 33.3f;
    for (u32 i = 0; i < snap.FrameCount; ++i)
        maxMs = std::max(maxMs, snap.FrameMsGraph[i]);
    if (snap.FrameCount > 0) {
        const f32 slot = graphW / static_cast<f32>(kProfileGraphSamples);
        const f32 barW = std::max(slot - 1.0f, 1.0f);
        for (u32 i = 0; i < snap.FrameCount; ++i) {
            const f32 ms = snap.FrameMsGraph[i];
            const f32 h = std::clamp(ms / maxMs, 0.0f, 1.0f) * graphH;
            if (h < 0.5f)
                continue;
            const Vec4 color =
                ms > 20.0f ? Vec4{0.90f, 0.35f, 0.30f, 1.0f} : Vec4{0.20f, 0.82f, 0.45f, 1.0f};
            const f32 column = static_cast<f32>(kProfileGraphSamples - snap.FrameCount + i);
            renderer.FillRect({x + slot * column, y + graphH - h}, {barW, h}, color);
        }
    }
    const f32 guide = std::clamp(16.7f / maxMs, 0.0f, 1.0f) * graphH;
    renderer.FillRect({x, y + graphH - guide}, {graphW, std::max(scale * 0.5f, 1.0f)},
                      {0.45f, 0.55f, 0.48f, 0.75f});
    y += graphH + 6.0f;

    for (u32 i = 0; i < snap.TopCount; ++i) {
        const ProfileScopeStat& scope = snap.Top[i];
        std::snprintf(buf, sizeof(buf), "%.24s %.3f ms self %.3f x%u", scope.Name, scope.TotalMs,
                      scope.SelfMs, scope.Calls);
        DrawDebugText(renderer, {x, y}, buf, text, scale);
        y += line;
    }

    std::snprintf(buf, sizeof(buf), "Draws %u  lines %u  sprites %u", snap.DrawCalls, snap.Lines,
                  snap.Sprites);
    DrawDebugText(renderer, {x, y}, buf, text, scale);
    y += line;
    std::snprintf(buf, sizeof(buf), "Arena %llu/%llu KiB peak %llu ovf %llu",
                  static_cast<unsigned long long>(snap.ArenaBytes / 1024),
                  static_cast<unsigned long long>(snap.ArenaCapacity / 1024),
                  static_cast<unsigned long long>(snap.ArenaPeakBytes / 1024),
                  static_cast<unsigned long long>(snap.ArenaOverflowFrames));
    DrawDebugText(renderer, {x, y}, buf, text, scale);
    y += line;
    std::snprintf(buf, sizeof(buf), "Pools %llu KiB (peak %llu)",
                  static_cast<unsigned long long>(snap.PoolBytes / 1024),
                  static_cast<unsigned long long>(snap.PoolPeakBytes / 1024));
    DrawDebugText(renderer, {x, y}, buf, text, scale);
    y += line;
    DrawDebugText(renderer, {x, y}, "F3 hide  F4 trace", dim, scale);
    renderer.End();
    renderer.SetLightingEnabled(wasLit);
}

} // namespace Emerald

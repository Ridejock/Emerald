// Profiler bookkeeping without a GPU: nested scopes, the disabled path, and Chrome trace JSON.

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <Emerald/Core/Log.h>
#include <Emerald/Core/Profile.h>
#include <Emerald/Core/Profiler.h>
#include <Emerald/Renderer/Renderer2D.h>

#include "Test.h"

using namespace Emerald;

namespace {

u64 g_Clock = 0;
u32 g_ClockReads = 0;

u64 TestClock()
{
    ++g_ClockReads;
    return g_Clock;
}

const ProfileScopeStat* FindScope(const ProfileSnapshot& snap, const char* name)
{
    for (u32 i = 0; i < snap.TopCount; ++i) {
        if (snap.Top[i].Name != nullptr && std::strcmp(snap.Top[i].Name, name) == 0)
            return &snap.Top[i];
    }
    return nullptr;
}

bool Near(f64 value, f64 expected)
{
    return std::abs(value - expected) < 1e-6;
}

// Braces and brackets balance, outside of strings. Enough to catch a broken export.
bool Balanced(std::string_view json)
{
    int braces = 0;
    int brackets = 0;
    bool inString = false;
    bool escape = false;
    for (const char c : json) {
        if (inString) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
            inString = true;
        else if (c == '{')
            ++braces;
        else if (c == '}')
            --braces;
        else if (c == '[')
            ++brackets;
        else if (c == ']')
            --brackets;
        if (braces < 0 || brackets < 0)
            return false;
    }
    return braces == 0 && brackets == 0 && !inString;
}

struct Fixture {
    Fixture() { Reset(); }
    ~Fixture() { Reset(); }

    static void Reset()
    {
        Log::Init({});
        g_Clock = 0;
        g_ClockReads = 0;
        Profiler& profiler = Profiler::Get();
        profiler.SetTracePath({});
        profiler.SetOverlayVisible(false);
        profiler.SetEnabled(false);
        profiler.SetClock(nullptr);
        profiler.Clear();
    }
};

} // namespace

#if EMERALD_PROFILER

TEST(ProfileDisabledPathDoesNotRecord)
{
    static_assert(sizeof(detail::ProfileScope) == 1, "a disabled scope guard is one bool");
    [[maybe_unused]] Fixture fix;
    Profiler& profiler = Profiler::Get();
    profiler.SetClock(&TestClock);
    profiler.SetEnabled(false);
    profiler.BeginFrame();
    for (int i = 0; i < 1000; ++i) {
        EM_PROFILE_SCOPE("Nope");
    }
    g_Clock = 1'000'000;
    profiler.EndFrame({});
    CHECK(g_ClockReads == 0);
    CHECK(profiler.GetSnapshot().FrameCount == 0);
    CHECK(profiler.GetSnapshot().TopCount == 0);
    CHECK(profiler.ExportChromeTrace() == "{\"traceEvents\":[]}\n");

    // Recording on, but no frame open: still no clock read.
    profiler.SetEnabled(true);
    {
        EM_PROFILE_SCOPE("Outside");
    }
    CHECK(g_ClockReads == 0);
    profiler.EndScope(); // nothing was pushed
}

TEST(ProfileNestedScopes)
{
    [[maybe_unused]] Fixture fix;
    Profiler& profiler = Profiler::Get();
    profiler.SetClock(&TestClock);
    profiler.SetEnabled(true);
    g_Clock = 0;
    profiler.BeginFrame();
    g_Clock = 1'000;
    {
        EM_PROFILE_SCOPE("Update");
        g_Clock = 5'000;
        {
            EM_PROFILE_SCOPE("Audio");
            g_Clock = 9'000;
        }
        g_Clock = 10'000;
    }
    g_Clock = 16'000'000;
    ProfileFrameStats stats;
    stats.DrawCalls = 4;
    stats.Lines = 10;
    stats.Sprites = 2;
    stats.ArenaBytes = 100;
    stats.ArenaCapacity = 2048;
    stats.ArenaPeakBytes = 200;
    stats.ArenaOverflowFrames = 3;
    stats.PoolBytes = 50;
    stats.PoolPeakBytes = 80;
    profiler.EndFrame(stats);

    const ProfileSnapshot& snap = profiler.GetSnapshot();
    CHECK(snap.FrameCount == 1);
    CHECK(Near(snap.FrameMs, 16.0));
    CHECK(snap.DrawCalls == 4 && snap.Lines == 10 && snap.Sprites == 2);
    CHECK(snap.ArenaBytes == 100 && snap.ArenaCapacity == 2048 && snap.ArenaPeakBytes == 200);
    CHECK(snap.ArenaOverflowFrames == 3);
    CHECK(snap.PoolBytes == 50 && snap.PoolPeakBytes == 80);
    CHECK(!snap.GpuValid);

    const ProfileScopeStat* update = FindScope(snap, "Update");
    const ProfileScopeStat* audio = FindScope(snap, "Audio");
    CHECK(update != nullptr && audio != nullptr);
    CHECK(update && update->Calls == 1 && Near(update->TotalMs, 0.009) &&
          Near(update->SelfMs, 0.005));
    CHECK(audio && audio->Calls == 1 && Near(audio->TotalMs, 0.004) && Near(audio->SelfMs, 0.004));
    // Heavier self time is listed first.
    CHECK(snap.TopCount >= 2 && std::strcmp(snap.Top[0].Name, "Update") == 0);

    const std::string json = profiler.ExportChromeTrace();
    CHECK(json.starts_with("{\"traceEvents\":["));
    CHECK(json.ends_with("]}\n"));
    CHECK(json.find(",]") == std::string::npos);
    CHECK(Balanced(json));
    CHECK(json.find("\"name\":\"Update\"") != std::string::npos);
    CHECK(json.find("\"cat\":\"cpu\"") != std::string::npos);
    CHECK(json.find("\"ph\":\"X\"") != std::string::npos);
    CHECK(json.find("\"dur\":9.000") != std::string::npos);
    CHECK(json.find("\"ts\":1.000") != std::string::npos);
    CHECK(json.find("\"dur\":4.000") != std::string::npos);
    CHECK(json.find("\"name\":\"frame\"") != std::string::npos);
    CHECK(json.find("\"dur\":16000.000") != std::string::npos);
    CHECK(json.find("\"drawCalls\":4,\"lines\":10,\"sprites\":2,\"arena\":100,\"pool\":50") !=
          std::string::npos);
    CHECK(json.find("\"args\":{\"depth\":0}") != std::string::npos);
    CHECK(json.find("\"args\":{\"depth\":1}") != std::string::npos);
}

TEST(ProfileDeepSelfTimeAndRepeatedName)
{
    [[maybe_unused]] Fixture fix;
    Profiler& profiler = Profiler::Get();
    profiler.SetClock(&TestClock);
    profiler.SetEnabled(true);
    profiler.BeginFrame();
    {
        EM_PROFILE_SCOPE("A");
        g_Clock = 10'000;
        {
            EM_PROFILE_SCOPE("B");
            g_Clock = 20'000;
            {
                EM_PROFILE_SCOPE("C");
                g_Clock = 30'000;
            }
            g_Clock = 40'000;
        }
        g_Clock = 100'000;
    }
    // Same name twice: totals add, self time does not subtract a sibling.
    {
        EM_PROFILE_SCOPE("A");
        g_Clock = 105'000;
    }
    profiler.EndFrame({});

    const ProfileSnapshot& snap = profiler.GetSnapshot();
    const ProfileScopeStat* a = FindScope(snap, "A");
    const ProfileScopeStat* b = FindScope(snap, "B");
    const ProfileScopeStat* c = FindScope(snap, "C");
    CHECK(a && a->Calls == 2 && Near(a->TotalMs, 0.105) && Near(a->SelfMs, 0.075));
    CHECK(b && b->Calls == 1 && Near(b->TotalMs, 0.030) && Near(b->SelfMs, 0.020));
    CHECK(c && c->Calls == 1 && Near(c->TotalMs, 0.010) && Near(c->SelfMs, 0.010));
}

TEST(ProfileFrameGraphGpuAndTraceFile)
{
    [[maybe_unused]] Fixture fix;
    Profiler& profiler = Profiler::Get();
    profiler.SetClock(&TestClock);
    profiler.SetEnabled(true);

    profiler.ReportGpuTime(-1.0);
    profiler.ReportGpuTime(std::numeric_limits<f64>::quiet_NaN());
    profiler.BeginFrame();
    g_Clock = 10'000'000;
    profiler.EndFrame({});
    CHECK(!profiler.GetSnapshot().GpuValid);
    CHECK(Near(profiler.GetSnapshot().FrameMs, 10.0));

    profiler.ReportGpuTime(1.5);
    profiler.BeginFrame();
    g_Clock = 30'000'000;
    {
        EM_PROFILE_SCOPE("say \"hi\"\\");
        g_Clock = 30'001'000;
    }
    g_Clock = 30'000'000; // the scope above already closed; this is the frame boundary
    profiler.EndFrame({});

    const ProfileSnapshot& snap = profiler.GetSnapshot();
    CHECK(snap.FrameCount == 2);
    CHECK(Near(snap.FrameMsGraph[0], 10.0));
    CHECK(Near(snap.FrameMsGraph[1], 20.0));
    CHECK(Near(snap.AverageMs, 15.0));
    CHECK(snap.GpuValid && Near(snap.GpuMs, 1.5));

    const std::string json = profiler.ExportChromeTrace();
    CHECK(Balanced(json));
    CHECK(json.find("\"name\":\"say \\\"hi\\\"\\\\\"") != std::string::npos);
    CHECK(json.find("\"name\":\"GPU\"") != std::string::npos);
    CHECK(json.find("\"cat\":\"gpu\"") != std::string::npos);
    CHECK(json.find("\"dur\":1500.000") != std::string::npos);
    int frames = 0;
    for (usize at = 0; (at = json.find("\"name\":\"frame\"", at)) != std::string::npos; ++at)
        ++frames;
    CHECK(frames == 2);

    std::error_code error;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path(error) / "emerald-profile-trace.json";
    CHECK(!error);
    CHECK(profiler.SaveChromeTrace(path));
    std::ifstream in(path);
    const std::string file((std::istreambuf_iterator<char>(in)), {});
    CHECK(file == json);
    std::filesystem::remove(path, error);
    CHECK(!profiler.SaveChromeTrace({}));
}

TEST(ProfileWorkerScopeIsTracedNotAggregated)
{
    [[maybe_unused]] Fixture fix;
    Profiler& profiler = Profiler::Get();
    profiler.SetClock(&TestClock);
    profiler.SetEnabled(true);
    profiler.BeginFrame();
    std::thread worker([] {
        EM_PROFILE_SCOPE("Worker");
        g_Clock = 5'000;
    });
    worker.join();
    g_Clock = 8'000;
    profiler.EndFrame({});
    CHECK(FindScope(profiler.GetSnapshot(), "Worker") == nullptr);
    CHECK(profiler.ExportChromeTrace().find("\"name\":\"Worker\"") != std::string::npos);
}

TEST(ProfileOverlayTextAndHideKeepsTrace)
{
    [[maybe_unused]] Fixture fix;
    Profiler& profiler = Profiler::Get();
    profiler.SetTracePath("trace.json");
    profiler.SetOverlayVisible(true);
    profiler.SetOverlayVisible(false);
    CHECK(profiler.IsEnabled()); // a trace path keeps recording after the panel is hidden
    profiler.SetTracePath({});
    profiler.SetOverlayVisible(false);
    CHECK(!profiler.IsEnabled());

    profiler.SetOverlayVisible(true);
    Renderer2D renderer;
    profiler.DrawOverlay(renderer, 1280.0f, 720.0f);
    CHECK(renderer.GetSpriteCount() > 20);
    profiler.SetOverlayVisible(false);
    Renderer2D hidden;
    profiler.DrawOverlay(hidden, 1280.0f, 720.0f);
    CHECK(hidden.GetSpriteCount() == 0);
}

#else

TEST(ProfileMacroCompilesAway)
{
    EM_PROFILE_SCOPE("gone");
    EM_PROFILE_FUNCTION();
    EM_PROFILE_FRAME();
    CHECK(!Profiler::Get().IsEnabled());
}

#endif

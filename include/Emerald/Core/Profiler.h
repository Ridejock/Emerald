#pragma once

// In-game profiler: per-frame CPU scopes, a frame-time history, Renderer2D and memory stats, and
// an optional Chrome trace. The EM_PROFILE_SCOPE macro (Profile.h) is the way to add a scope; this
// header is the overlay, the capture and the numbers.
//
// Recording is off until something turns it on (F3, --profile, SetOverlayVisible, SetEnabled).
// While it is off, EM_PROFILE_SCOPE is one relaxed load and a branch — nothing is stored. Build
// with -DEMERALD_PROFILER=OFF and the macro compiles away entirely (Tracy, if EMERALD_PROFILE is
// on, is unaffected).
//
// GPU time: SDL GPU 3.4 has no timestamp-query API, so the renderer never has a sample and the
// overlay shows "n/a" on every backend (Vulkan, D3D12, Metal). ReportGpuTime feeds a sample when
// a backend, or a test, can measure one. The value stays until the next sample or Clear.
//
// Names passed to scopes must outlive the capture (string literals and __func__ do). The trace is
// JSON written by hand; this header does not include a JSON library.

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

#include "Emerald/Core/Defines.h"

namespace Emerald {

class Renderer2D;

inline constexpr u32 kProfileGraphSamples = 120;
inline constexpr u32 kProfileTopScopes = 8;

// One aggregated scope over the frame that just finished (main thread).
struct ProfileScopeStat {
    const char* Name = "";
    f64 TotalMs = 0; // wall time, children included
    f64 SelfMs = 0;  // total minus direct children
    u32 Calls = 0;
};

// What the overlay draws. Filled at EndFrame from the frame that just closed.
struct ProfileSnapshot {
    f64 FrameMs = 0;   // real time from BeginFrame to EndFrame
    f64 AverageMs = 0; // mean of FrameMsGraph
    f64 GpuMs = 0;
    bool GpuValid = false;
    u32 DrawCalls = 0;
    u32 Lines = 0;
    u32 Sprites = 0;
    usize ArenaBytes = 0;
    usize ArenaCapacity = 0;
    usize ArenaPeakBytes = 0;
    u64 ArenaOverflowFrames = 0;
    usize PoolBytes = 0;
    usize PoolPeakBytes = 0;
    std::array<f32, kProfileGraphSamples> FrameMsGraph{};
    u32 FrameCount = 0; // samples stored in FrameMsGraph (oldest first)
    std::array<ProfileScopeStat, kProfileTopScopes> Top{};
    u32 TopCount = 0;
};

// Batch and memory counters for the frame that is ending. The profiler times the frame itself.
struct ProfileFrameStats {
    u32 DrawCalls = 0;
    u32 Lines = 0;
    u32 Sprites = 0;
    usize ArenaBytes = 0;
    usize ArenaCapacity = 0;
    usize ArenaPeakBytes = 0;
    u64 ArenaOverflowFrames = 0;
    usize PoolBytes = 0;
    usize PoolPeakBytes = 0;
};

// Process-wide profiler. Application drives BeginFrame / EndFrame and the overlay; games add
// scopes with EM_PROFILE_SCOPE and otherwise leave this alone.
class Profiler {
public:
    static Profiler& Get();
    ~Profiler();

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // Recording switch. The overlay and a --profile-trace path turn it on; SetEnabled is the
    // direct switch (tests, and a capture that should keep running while the overlay is hidden).
    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    void SetOverlayVisible(bool visible);
    [[nodiscard]] bool IsOverlayVisible() const { return m_OverlayVisible; }
    void ToggleOverlay() { SetOverlayVisible(!m_OverlayVisible); }

    // Where F4 / shutdown write a Chrome trace. A non-empty path also keeps recording on after
    // the overlay is hidden. Empty clears the path and does not by itself disable recording.
    void SetTracePath(std::filesystem::path path);
    [[nodiscard]] const std::filesystem::path& GetTracePath() const { return m_TracePath; }

    // A frame. Scopes only record while a frame is open (so startup work is not glued onto frame
    // 1). EndFrame snapshots stats, updates the graph and appends the frame to the trace.
    void BeginFrame();
    void EndFrame(const ProfileFrameStats& stats);

    // Low-level scope, used by EM_PROFILE_SCOPE. False when recording is off or no frame is open
    // (the caller must not call EndScope then). Name is stored, not copied.
    bool BeginScope(const char* name);
    void EndScope();

    // Latest GPU frame time, in milliseconds. Ignored when not finite or negative.
    void ReportGpuTime(f64 milliseconds);

    [[nodiscard]] const ProfileSnapshot& GetSnapshot() const { return m_Snapshot; }

    // Chrome trace (chrome://tracing or Perfetto) of the frames still held, oldest first.
    [[nodiscard]] std::string ExportChromeTrace() const;
    // Writes ExportChromeTrace to `path` (parent directories are created). False on an empty path
    // or a failed write; the reason is logged.
    bool SaveChromeTrace(const std::filesystem::path& path) const;

    // ImGui window when the overlay is visible and the build has ImGui; otherwise does nothing.
    void DrawImGui();
    // Basic text panel (no ImGui). Begins and ends its own Renderer2D batch. No-op when the
    // overlay is hidden. `pixelWidth` / `pixelHeight` are the render target, in pixels.
    void DrawOverlay(Renderer2D& renderer, f32 pixelWidth, f32 pixelHeight) const;

    // Drops recorded frames, the graph and GPU sample. Does not change enabled, the overlay or
    // the trace path. Safe to call from tests between cases.
    void Clear();

    // Steady clock, or this function (nanoseconds, any origin). Null restores the steady clock.
    // Set it before scopes run on other threads.
    void SetClock(u64 (*clock)());

private:
    Profiler();

    struct State;
    std::unique_ptr<State> m_State;

    [[nodiscard]] u64 Now() const;

    bool m_OverlayVisible = false;
    std::filesystem::path m_TracePath;
    ProfileSnapshot m_Snapshot;
};

// The recording flag EM_PROFILE_SCOPE checks inline. One process-wide flag (function-local
// static), relaxed: a scope that starts a moment late or early is fine, and the disabled path
// must not take a lock.
[[nodiscard]] inline std::atomic<bool>& ProfilerRecordingFlag() noexcept
{
    static std::atomic<bool> flag{false};
    return flag;
}

[[nodiscard]] inline bool ProfilerRecording() noexcept
{
    return ProfilerRecordingFlag().load(std::memory_order_relaxed);
}

} // namespace Emerald

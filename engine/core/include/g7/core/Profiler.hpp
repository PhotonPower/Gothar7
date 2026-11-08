#pragma once

#include <g7/core/Types.hpp>

#include <chrono>
#include <string_view>
#include <vector>

/// Profiling hook. The macros below are the only thing engine/game code uses; they compile to
/// nothing unless the CMake option G7_PROFILING is ON. Behind them sits a small built-in
/// per-frame collector (debug overlay, --perf), and with G7_TRACY (preset profile, ADR 0024) Tracy as well.
namespace g7::profiler
{
/// Aggregated timings of one zone name within one frame. Times are inclusive (nested zones
/// count towards their parent as well).
struct ZoneStats
{
    std::string_view name;
    u32 calls = 0;
    f64 totalMs = 0.0;
    f64 maxMs = 0.0;
};

/// Measures from construction to destruction. `name` must have static storage duration
/// (string literal, __func__) – the same rule Tracy has. Zones are thread-safe; a zone counts
/// towards the frame in which it ends.
class ScopedZone
{
public:
    explicit ScopedZone(const char* name) noexcept;
    ~ScopedZone();

    ScopedZone(const ScopedZone&) = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;

private:
    const char* m_name;
    std::chrono::steady_clock::time_point m_start;
};

/// Closes the current frame: its zones become lastFrame(), collection starts afresh.
void markFrame();

/// Zones of the last closed frame, sorted by total time (descending).
[[nodiscard]] std::vector<ZoneStats> lastFrame();
} // namespace g7::profiler

#define G7_PROFILE_CONCAT_INNER(a, b) a##b
#define G7_PROFILE_CONCAT(a, b) G7_PROFILE_CONCAT_INNER(a, b)

// Tracy (ADR 0024, preset profile): the same zones go to Tracy as well; names must be string literals there.
#if defined(G7_TRACY) && G7_TRACY
#include <tracy/Tracy.hpp>
#define G7_PROFILE_TRACY_ZONE(name) ZoneNamedN(G7_PROFILE_CONCAT(g7TracyZone, __LINE__), name, true)
#define G7_PROFILE_TRACY_FUNCTION() ZoneNamed(G7_PROFILE_CONCAT(g7TracyZone, __LINE__), true)
#define G7_PROFILE_TRACY_FRAME() FrameMark
#define G7_PROFILE_THREAD(name) tracy::SetThreadName(name)
#else
#define G7_PROFILE_TRACY_ZONE(name) static_cast<void>(0)
#define G7_PROFILE_TRACY_FUNCTION() static_cast<void>(0)
#define G7_PROFILE_TRACY_FRAME() static_cast<void>(0)
#define G7_PROFILE_THREAD(name) static_cast<void>(0)
#endif

#if defined(G7_PROFILING) && G7_PROFILING
#define G7_PROFILE_SCOPE(name)                                                                               \
    G7_PROFILE_TRACY_ZONE(name);                                                                             \
    const ::g7::profiler::ScopedZone G7_PROFILE_CONCAT(g7ProfileZone, __LINE__)(name)
#define G7_PROFILE_FUNCTION()                                                                                \
    G7_PROFILE_TRACY_FUNCTION();                                                                             \
    const ::g7::profiler::ScopedZone G7_PROFILE_CONCAT(g7ProfileZone, __LINE__)(__func__)
#define G7_PROFILE_FRAME()                                                                                   \
    do                                                                                                       \
    {                                                                                                        \
        ::g7::profiler::markFrame();                                                                         \
        G7_PROFILE_TRACY_FRAME();                                                                            \
    } while (false)
#else
#define G7_PROFILE_SCOPE(name) static_cast<void>(0)
#define G7_PROFILE_FUNCTION() static_cast<void>(0)
#define G7_PROFILE_FRAME() static_cast<void>(0)
#endif

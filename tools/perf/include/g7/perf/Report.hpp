#pragma once

// The result of a perf run (docs/modules/perf.md part B): perf_<scenario>.json and one log line per run.

#include <g7/core/Types.hpp>
#include <g7/runtime/FrameTimes.hpp>

#include <string>
#include <vector>

namespace g7::perf
{
/// One zone of the built-in profiler: its time per frame (all calls added up) over the measured frames.
struct ZoneResult
{
    std::string name;
    FrameTimeSummary ms;
    f64 callsPerFrame = 0.0;
};

/// A GPU pass: its (smoothed) timer-query time, averaged over the measured frames.
struct PassResult
{
    std::string name;
    f64 averageMs = 0.0;
};

/// One scenario at one time of day.
struct RunResult
{
    u32 minuteOfDay = 0;
    FrameTimeSummary frame;
    std::vector<ZoneResult> zones; ///< empty without G7_PROFILING
    std::vector<PassResult> gpu;
    f64 drawCalls = 0.0; ///< averages per frame
    f64 triangles = 0.0;
    f64 lights = 0.0;
    u32 creatures = 0; ///< at the end of the run
    f64 simulatedCreatures = 0.0;
    u32 simulatedMax = 0;
};

struct Report
{
    std::string scenario;
    std::string build;    ///< BuildInfo or engine version
    std::string renderer; ///< GPU
    u32 width = 0;
    u32 height = 0;
    bool profiling = false;
    std::vector<RunResult> runs;
    std::vector<LoadPhase> loadPhases;
};

/// The report as JSON (pretty, stable key order).
[[nodiscard]] std::string toJson(const Report& report);
/// "perf markt 10:00: 600 frames: ... ; GPU 6.10 ms; 812 draws, 1.9M tris, 14 lights, 18/42 NPCs simulated"
[[nodiscard]] std::string summaryLine(const std::string& scenario, const RunResult& run);
/// "10:00"
[[nodiscard]] std::string clockText(u32 minuteOfDay);
} // namespace g7::perf

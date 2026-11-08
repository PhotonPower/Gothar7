#pragma once

// gothar --perf=<names> (docs/modules/perf.md part B): puts camera and hero on each scenario's view, sets the
// time, warms up, measures a fixed number of frames and writes perf_<scenario>.json, then quits.

#include <g7/core/FileSystem.hpp>
#include <g7/perf/Report.hpp>
#include <g7/perf/Scenario.hpp>
#include <g7/runtime/EngineTool.hpp>
#include <g7/runtime/FrameTimes.hpp>

#include <map>
#include <string>
#include <vector>

namespace g7::perf
{
class PerfRun final : public EngineTool
{
public:
    /// `names`: as for selectScenarios; `file`: perf.toml in the VFS; reports go to `outDir`.
    PerfRun(std::string names, std::string file, fs::Path outDir, u32 width, u32 height);

    void update(Engine& engine, f64 realSeconds, bool uiMouse, bool uiKeyboard) override;
    void ui(Engine& engine, ui::DebugUi& debugUi) override;

    /// False when loading or writing failed (exit code of the game).
    [[nodiscard]] bool succeeded() const noexcept { return m_ok; }

private:
    enum class Phase
    {
        Load,
        Start,
        Warmup,
        Measure,
        Done
    };
    void fail(Engine& engine, const std::string& message);
    void startRun(Engine& engine);
    void measure(Engine& engine, f64 realSeconds);
    void spawnExtraNpcs(Engine& engine, const Scenario& s);
    void finishRun(Engine& engine);
    void finishScenario(Engine& engine);

    std::string m_names;
    std::string m_file;
    fs::Path m_outDir;
    u32 m_width = 0;
    u32 m_height = 0;
    PerfFile m_perf;
    std::vector<Scenario> m_scenarios;
    usize m_scenario = 0;
    usize m_time = 0;
    Phase m_phase = Phase::Load;
    f64 m_warmup = 0.0;
    u32 m_frame = 0;
    bool m_ok = true;
    bool m_spawned = false;

    Report m_report;
    FrameTimes m_frameTimes;
    std::map<std::string, FrameTimes> m_zoneTimes;
    std::map<std::string, u64> m_zoneCalls;
    std::map<std::string, f64> m_passSums;
    std::vector<std::string> m_passOrder;
    f64 m_draws = 0.0;
    f64 m_triangles = 0.0;
    f64 m_lights = 0.0;
    f64 m_simulated = 0.0;
    u32 m_simulatedMax = 0;
};
} // namespace g7::perf

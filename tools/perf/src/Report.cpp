#include <g7/perf/Report.hpp>

#include <nlohmann/json.hpp>

#include <format>

namespace g7::perf
{
namespace
{
nlohmann::ordered_json summaryJson(const FrameTimeSummary& s)
{
    return {{"frames", s.frames}, {"average_ms", s.averageMs}, {"p50_ms", s.p50Ms},
            {"p95_ms", s.p95Ms},  {"p99_ms", s.p99Ms},         {"worst_ms", s.worstMs}};
}
} // namespace

std::string clockText(u32 minuteOfDay)
{
    return std::format("{:02}:{:02}", (minuteOfDay / 60) % 24, minuteOfDay % 60);
}

std::string toJson(const Report& report)
{
    nlohmann::ordered_json j;
    j["scenario"] = report.scenario;
    j["build"] = report.build;
    j["renderer"] = report.renderer;
    j["resolution"] = {report.width, report.height};
    j["profiling"] = report.profiling;
    nlohmann::ordered_json runs = nlohmann::ordered_json::array();
    for (const RunResult& run : report.runs)
    {
        nlohmann::ordered_json r;
        r["time"] = clockText(run.minuteOfDay);
        r["frame"] = summaryJson(run.frame);
        nlohmann::ordered_json zones = nlohmann::ordered_json::array();
        for (const ZoneResult& zone : run.zones)
        {
            nlohmann::ordered_json z = summaryJson(zone.ms);
            z.erase("frames");
            z["name"] = zone.name;
            z["calls_per_frame"] = zone.callsPerFrame;
            zones.push_back(std::move(z));
        }
        r["zones"] = std::move(zones);
        nlohmann::ordered_json gpu = nlohmann::ordered_json::array();
        f64 gpuTotal = 0.0;
        for (const PassResult& pass : run.gpu)
        {
            gpu.push_back({{"name", pass.name}, {"average_ms", pass.averageMs}});
            gpuTotal += pass.averageMs;
        }
        r["gpu_passes"] = std::move(gpu);
        r["gpu_total_ms"] = gpuTotal;
        r["stats"] = {{"draw_calls", run.drawCalls},
                      {"triangles", run.triangles},
                      {"lights", run.lights},
                      {"npcs", run.creatures},
                      {"npcs_simulated", run.simulatedCreatures},
                      {"npcs_simulated_max", run.simulatedMax}};
        runs.push_back(std::move(r));
    }
    j["runs"] = std::move(runs);
    nlohmann::ordered_json load = nlohmann::ordered_json::array();
    for (const LoadPhase& phase : report.loadPhases)
    {
        load.push_back({{"name", phase.name}, {"ms", phase.ms}, {"end_ms", phase.endMs}});
    }
    j["load_phases"] = std::move(load);
    return j.dump(2) + "\n";
}

std::string summaryLine(const std::string& scenario, const RunResult& run)
{
    f64 gpu = 0.0;
    for (const PassResult& pass : run.gpu)
    {
        gpu += pass.averageMs;
    }
    return std::format(
        "perf {} {}: {}; GPU {:.2f} ms; {:.0f} draws, {:.2f}M tris, {:.0f} lights, {:.1f}/{} NPCs "
        "simulated",
        scenario, clockText(run.minuteOfDay), run.frame.toString(), gpu, run.drawCalls, run.triangles / 1.0e6,
        run.lights, run.simulatedCreatures, run.creatures);
}
} // namespace g7::perf

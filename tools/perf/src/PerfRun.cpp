#include <g7/ai/Waynet.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/core/Version.hpp>
#include <g7/perf/PerfRun.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/runtime/StartView.hpp>

#include <algorithm>
#include <format>
#include <span>

namespace g7::perf
{
namespace
{
/// Longest wait for streamed models before measuring anyway.
constexpr f64 kMaxWarmupSeconds = 120.0;

bool modelsSettled(const Engine& engine)
{
    const auto& phases = engine.loadPhases();
    return std::any_of(phases.begin(), phases.end(),
                       [](const LoadPhase& p) { return p.name == "models settled"; });
}
} // namespace

PerfRun::PerfRun(std::string names, std::string file, fs::Path outDir, u32 width, u32 height)
    : m_names(std::move(names)), m_file(std::move(file)), m_outDir(std::move(outDir)), m_width(width),
      m_height(height)
{
}

void PerfRun::fail(Engine& engine, const std::string& message)
{
    G7_LOG_ERROR("perf", "{}", message);
    m_ok = false;
    m_phase = Phase::Done;
    engine.requestQuit();
}

void PerfRun::update(Engine& engine, f64 realSeconds, bool, bool)
{
    switch (m_phase)
    {
    case Phase::Load:
    {
        auto bytes = engine.vfs().read(m_file);
        if (!bytes)
        {
            fail(engine, std::format("--perf: {}", bytes.error().message));
            return;
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes.value().data()),
                                    bytes.value().size());
        auto parsed = parsePerfFile(text, m_file);
        if (!parsed)
        {
            fail(engine, std::format("--perf: {}", parsed.error().message));
            return;
        }
        m_perf = std::move(parsed).value();
        auto selected = selectScenarios(m_perf, m_names);
        if (!selected)
        {
            fail(engine, std::format("--perf: {}", selected.error().message));
            return;
        }
        m_scenarios = std::move(selected).value();
        startRun(engine);
        return;
    }
    case Phase::Start:
        startRun(engine);
        return;
    case Phase::Warmup:
        m_warmup += realSeconds;
        if (m_warmup >= m_perf.warmupSeconds && (modelsSettled(engine) || m_warmup >= kMaxWarmupSeconds))
        {
            if (!modelsSettled(engine))
            {
                G7_LOG_WARN("perf", "models still loading after {:.0f} s, measuring anyway", m_warmup);
            }
            m_phase = Phase::Measure;
            m_frame = 0;
        }
        return;
    case Phase::Measure:
        measure(engine, realSeconds);
        return;
    case Phase::Done:
        return;
    }
}

void PerfRun::measure(Engine& engine, f64 realSeconds)
{
    // `realSeconds`, the zones, the GPU passes and the stats all belong to the frame before this one.
    if (++m_frame == 1)
    {
        return; // that frame was still warm-up
    }
    m_frameTimes.add(realSeconds);
    std::map<std::string, f64> frameZones;
    for (const profiler::ZoneStats& zone : profiler::lastFrame())
    {
        const std::string name(zone.name);
        frameZones[name] += zone.totalMs;
        m_zoneCalls[name] += zone.calls;
    }
    for (auto& [name, times] : m_zoneTimes)
    {
        if (!frameZones.contains(name))
        {
            times.add(0.0); // the zone did not run this frame
        }
    }
    for (const auto& [name, ms] : frameZones)
    {
        FrameTimes& times = m_zoneTimes[name];
        while (times.size() + 1 < m_frameTimes.size())
        {
            times.add(0.0); // a new zone: it did not run in the earlier frames
        }
        times.add(ms / 1000.0);
    }
    for (const render::PassTime& pass : engine.passTimes())
    {
        if (!m_passSums.contains(pass.name))
        {
            m_passOrder.push_back(pass.name);
        }
        m_passSums[pass.name] += pass.milliseconds;
    }
    const render::FrameStats stats = engine.frameStats();
    m_draws += stats.drawCalls;
    m_triangles += stats.triangles;
    m_lights += static_cast<f64>(engine.frameLights().lights().size());
    const auto simulated = static_cast<u32>(engine.simulatedCreatureCount());
    m_simulated += simulated;
    m_simulatedMax = std::max(m_simulatedMax, simulated);
    if (m_frameTimes.size() >= m_perf.frames)
    {
        finishRun(engine);
    }
}

void PerfRun::ui(Engine&, ui::DebugUi&)
{
}

void PerfRun::spawnExtraNpcs(Engine& engine, const Scenario& s)
{
    // Stress: NPCs on the way points nearest the hero (they walk their routines from there).
    std::vector<Vec3> spots;
    for (const ai::Waynet::Point& p : engine.waynet().points())
    {
        if (glm::distance(p.position, s.player) <= s.npcRadius)
        {
            spots.push_back(p.position);
        }
    }
    std::sort(spots.begin(), spots.end(), [&](const Vec3& a, const Vec3& b)
              { return glm::distance(a, s.player) < glm::distance(b, s.player); });
    if (spots.empty())
    {
        spots.push_back(s.player + Vec3(2.0f, 0.5f, 0.0f));
    }
    u32 spawned = 0;
    for (u32 i = 0; i < s.extraNpcs; ++i)
    {
        const auto round = static_cast<f32>(i / spots.size());
        const Vec3 at = spots[i % spots.size()] + Vec3(0.4f * round, 0.0f, 0.4f * round);
        if (auto id = engine.spawnScriptNpc(s.npcs[i % s.npcs.size()], at, static_cast<f32>(i) * 0.7f))
        {
            ++spawned;
        }
        else
        {
            G7_LOG_WARN("perf", "{}", id.error().message);
        }
    }
    G7_LOG_INFO("perf", "{} extra NPCs on {} way points", spawned, spots.size());
}

void PerfRun::startRun(Engine& engine)
{
    const Scenario& s = m_scenarios[m_scenario];
    if (m_time == 0)
    {
        m_report = {};
        m_report.scenario = s.name;
        m_report.build =
            std::format("{}.{}.{}", kEngineVersion.major, kEngineVersion.minor, kEngineVersion.patch);
        m_report.renderer = engine.rendererName();
        m_report.width = m_width;
        m_report.height = m_height;
#if defined(G7_PROFILING) && G7_PROFILING
        m_report.profiling = true;
#endif
        G7_LOG_INFO("perf", "scenario {}{}{}", s.name, s.description.empty() ? "" : ": ", s.description);
    }
    StartView view;
    view.camera = s.camera;
    view.yawDegrees = s.yawDegrees;
    view.pitchDegrees = s.pitchDegrees;
    view.player = s.player;
    const u32 minute = s.minutesOfDay[m_time];
    engine.gameTime().setTime(engine.gameTime().day(), minute / 60, minute % 60);
    engine.applyView(view); // after the time: its log line shows the measured time
    if (m_time == 0 && s.extraNpcs > 0)
    {
        spawnExtraNpcs(engine, s);
    }
    m_warmup = 0.0;
    m_frameTimes.clear();
    m_zoneTimes.clear();
    m_zoneCalls.clear();
    m_passSums.clear();
    m_passOrder.clear();
    m_draws = 0.0;
    m_triangles = 0.0;
    m_lights = 0.0;
    m_simulated = 0.0;
    m_simulatedMax = 0;
    m_phase = Phase::Warmup;
}

void PerfRun::finishRun(Engine& engine)
{
    const Scenario& s = m_scenarios[m_scenario];
    const auto frames = static_cast<f64>(m_frameTimes.size());
    RunResult run;
    run.minuteOfDay = s.minutesOfDay[m_time];
    run.frame = m_frameTimes.summary();
    for (const auto& [name, times] : m_zoneTimes)
    {
        run.zones.push_back({name, times.summary(), static_cast<f64>(m_zoneCalls[name]) / frames});
    }
    std::sort(run.zones.begin(), run.zones.end(),
              [](const ZoneResult& a, const ZoneResult& b) { return a.ms.averageMs > b.ms.averageMs; });
    for (const std::string& name : m_passOrder)
    {
        run.gpu.push_back({name, m_passSums[name] / frames});
    }
    run.drawCalls = m_draws / frames;
    run.triangles = m_triangles / frames;
    run.lights = m_lights / frames;
    run.creatures = static_cast<u32>(engine.creatureCount());
    run.simulatedCreatures = m_simulated / frames;
    run.simulatedMax = m_simulatedMax;
    G7_LOG_INFO("perf", "{}", summaryLine(s.name, run));
    m_report.runs.push_back(std::move(run));
    if (++m_time < s.minutesOfDay.size())
    {
        m_phase = Phase::Start;
        return;
    }
    finishScenario(engine);
}

void PerfRun::finishScenario(Engine& engine)
{
    m_report.loadPhases = engine.loadPhases();
    const std::string json = toJson(m_report);
    const fs::Path file = m_outDir / fs::fromUtf8(std::format("perf_{}.json", m_report.scenario));
    if (auto dir = fs::createDirectories(m_outDir); !dir)
    {
        fail(engine, std::format("--perf: {}", dir.error().message));
        return;
    }
    if (auto written = fs::writeFile(file, std::span(reinterpret_cast<const u8*>(json.data()), json.size()));
        !written)
    {
        fail(engine, std::format("--perf: {}", written.error().message));
        return;
    }
    G7_LOG_INFO("perf", "report written to {}", fs::toUtf8(file));
    m_time = 0;
    if (++m_scenario < m_scenarios.size())
    {
        m_phase = Phase::Start;
        return;
    }
    m_phase = Phase::Done;
    engine.requestQuit();
}
} // namespace g7::perf

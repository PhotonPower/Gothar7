// M17 perf part C2: the creatures animate in parallel (job system) - the result must not depend on the thread
// count. The same camp scenario (NPCs on their routines, a fight, a wolf pack, figures that are only
// animated) runs with one and with four job threads; the creatures' state after every half second must be the
// same to the byte.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;

namespace
{
script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

struct Trace
{
    std::vector<u32> digests; ///< creatureDigest() every 30 steps
    usize creatures = 0;
    std::string banditState;
};

Trace scenario(u32 threads)
{
    EngineConfig config;
    config.appName = "jobs";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.randomSeed = 7;
    config.settings.set<i64>("jobs.threads", threads);
    config.settings.set<i64>("ai.max_simulated", 6); // some NPCs drop out: visible ones are only animated
    Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.jobs().threads() == threads);

    // The camp's people on their routines, its animals.
    run(engine, "Story.met_gate_guard = true camp_people()");
    run(engine, "insert_animal('mon_keiler') insert_animal('mon_laufvogel')");
    // A fight: a bandit attacks the hero, who only stands and takes it.
    run(engine, "set_stat('hp_max', 5000) set_stat('hp', 5000) teleport(40, 0, 30)");
    REQUIRE(run(engine, "insert_npc('npc_bandit', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_bandit', '') npc_teleport('npc_bandit', 37, 0, 30, 270)");
    run(engine, "fight('npc_bandit', 'hero')");
    // A wolf pack at its den (animal AI).
    REQUIRE(run(engine, "#insert_pack('mon_wolf', 3, 'wp_wolf_den')").asInteger() == 3);
    // Figures that are only animated (no AI): animals going through all their clips, moved by root motion.
    for (const auto& [species, x] :
         {std::pair{"keiler", 30.0f}, std::pair{"laufvogel", 33.0f}, std::pair{"wolf", 36.0f}})
    {
        const auto id = engine.spawnCreature(species, Vec3(x, 0.0f, 26.0f), 0.5f);
        REQUIRE(id.ok());
        engine.setCreatureShowcase(id.value(), true);
    }

    Trace trace;
    for (int step = 1; step <= 1200; ++step)
    {
        REQUIRE(engine.runFrame());
        if (step % 30 == 0)
        {
            trace.digests.push_back(engine.creatureDigest());
        }
    }
    trace.creatures = engine.creatureCount();
    trace.banditState = std::string(run(engine, "fight_state('npc_bandit')").asString());
    return trace;
}
} // namespace

TEST_CASE("Jobs scenario: the creatures end the same with 1 and with 4 job threads")
{
    const Trace serial = scenario(1);
    const Trace parallel = scenario(4);
    REQUIRE(serial.digests.size() == parallel.digests.size());
    CHECK(serial.creatures >= 10);
    CHECK(serial.creatures == parallel.creatures);
    CHECK(serial.banditState == parallel.banditState);
    for (usize i = 0; i < serial.digests.size(); ++i)
    {
        CAPTURE(i);
        REQUIRE(serial.digests[i] == parallel.digests[i]);
    }
    // Something happened: the state changes along the run.
    CHECK(serial.digests.front() != serial.digests.back());
}

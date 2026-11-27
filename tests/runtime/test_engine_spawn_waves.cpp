// Spawn markers with targets and inactive ones (docs/modules/dungeon.md "Stand D6", extension E8) on
// testworld/spawn_waves.g7world: a marker tells its targets once all its NPCs are dead (a relay opens the
// arena gate), also across a save game and never twice; an inactive marker puts its NPCs in only on `on` or
// `trigger`, one time; the Kellermutter's phases each set one crawler wave going.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;

namespace
{
EngineConfig wavesConfig()
{
    EngineConfig config;
    config.appName = "spawn_waves";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/spawn_waves.g7world");
    config.start = "START_WELLEN";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    REQUIRE_MESSAGE(result.ok(), std::string(line), ": ", (result.ok() ? "" : result.error().message));
    return result.value();
}

void frames(Engine& engine, int n)
{
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

i64 living(Engine& engine, std::string_view spawn)
{
    return run(engine, std::format("#spawn_npcs('{}')", spawn)).asInteger();
}

std::vector<std::string> npcsOf(Engine& engine, std::string_view spawn)
{
    std::vector<std::string> names;
    const i64 n = living(engine, spawn);
    for (i64 i = 1; i <= n; ++i)
    {
        names.emplace_back(run(engine, std::format("spawn_npcs('{}')[{}]", spawn, i)).asString());
    }
    return names;
}

bool gateOpen(Engine& engine)
{
    return run(engine, "mover_state('GATE_ARENA').open").asBool();
}

void saveAndLoad(Engine& engine)
{
    auto saved = engine.captureGame();
    REQUIRE(saved.ok());
    REQUIRE(engine.applyGame(saved.value()).ok());
    frames(engine, 3);
}
} // namespace

TEST_CASE("Engine spawn waves: a marker tells its targets once all its NPCs are dead, once, across a save")
{
    Engine engine(wavesConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    frames(engine, 3);
    const auto pair = npcsOf(engine, "SPAWN_PAAR");
    REQUIRE(pair.size() == 2);
    CHECK_FALSE(gateOpen(engine));

    // One of two dead: nothing yet. Saved and loaded in between: the state is not lost.
    run(engine, std::format("npc_kill('{}')", pair[0]));
    frames(engine, 5);
    CHECK_FALSE(gateOpen(engine));
    saveAndLoad(engine);
    CHECK(living(engine, "SPAWN_PAAR") == 1);
    CHECK_FALSE(gateOpen(engine));

    // The second dead: the relay opens the gate.
    run(engine, std::format("npc_kill('{}')", pair[1]));
    frames(engine, 60);
    CHECK(gateOpen(engine));

    // Closed again and saved, loaded: the marker has fired - it does not again.
    run(engine, "send('GATE_ARENA', 'close')");
    frames(engine, 60);
    REQUIRE_FALSE(gateOpen(engine));
    saveAndLoad(engine);
    frames(engine, 60);
    CHECK_FALSE(gateOpen(engine));
    CHECK(engine.scripts()->callErrors() == 0);
}

TEST_CASE("Engine spawn waves: an inactive marker puts its NPCs in only on on/trigger, one time")
{
    Engine engine(wavesConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    CHECK(living(engine, "SPAWN_SPAETER") == 0);
    CHECK(living(engine, "SPAWN_E08_WELLE_1") == 0);
    run(engine, "send('SPAWN_SPAETER', 'wake') send('SPAWN_SPAETER', 'off')");
    frames(engine, 3);
    CHECK(living(engine, "SPAWN_SPAETER") == 0);

    run(engine, "send('SPAWN_SPAETER', 'on')");
    frames(engine, 3);
    CHECK(living(engine, "SPAWN_SPAETER") == 1);
    // Spent: neither another event nor a save game puts a second one in.
    run(engine, "send('SPAWN_SPAETER', 'trigger')");
    frames(engine, 3);
    CHECK(living(engine, "SPAWN_SPAETER") == 1);
    saveAndLoad(engine);
    CHECK(living(engine, "SPAWN_SPAETER") == 1);
    run(engine, std::format("npc_kill('{}')", npcsOf(engine, "SPAWN_SPAETER")[0]));
    run(engine, "send('SPAWN_SPAETER', 'on')");
    frames(engine, 3);
    CHECK(living(engine, "SPAWN_SPAETER") == 0);

    // trigger works as on (a waiting marker, activated after a save game).
    Engine fresh(wavesConfig());
    REQUIRE(fresh.init().ok());
    frames(fresh, 3);
    saveAndLoad(fresh);
    CHECK(living(fresh, "SPAWN_SPAETER") == 0);
    run(fresh, "send('SPAWN_SPAETER', 'trigger')");
    frames(fresh, 3);
    CHECK(living(fresh, "SPAWN_SPAETER") == 1);
}

TEST_CASE("Engine spawn waves: each of the Kellermutter's phases sets one crawler wave going")
{
    Engine engine(wavesConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "set_stat('hp_max', 5000) set_stat('hp', 5000)");
    const std::string mother(run(engine, "insert_npc('mon_kellermutter', 'WP_WELLEN_OST')").asString());
    run(engine, std::format("npc_set_dormant('{}', true)", mother));
    run(engine, std::format("npc_start_state('{}', 'zs_dormant', 'WP_WELLEN_OST')", mother));
    frames(engine, 5);
    CHECK(living(engine, "SPAWN_E08_WELLE_1") == 0);

    // The first exchange of blows begins phase 1: the first wave.
    run(engine, std::format("emit('npc_hit', 'hero', '{}', 20, false)", mother));
    frames(engine, 3);
    CHECK(run(engine, std::format("boss_phase('{}')", mother)).asInteger() == 1);
    CHECK(living(engine, "SPAWN_E08_WELLE_1") == 2);
    CHECK(living(engine, "SPAWN_E08_WELLE_2") == 0);
    // A second blow in phase 1 sends no more.
    run(engine, std::format("emit('npc_hit', 'hero', '{}', 20, false)", mother));
    frames(engine, 3);
    CHECK(living(engine, "SPAWN_E08_WELLE_1") == 2);

    // Under two thirds: phase 2, the second wave.
    run(engine, std::format("npc_set_stat('{}', 'hp', 380)", mother)); // of 600
    run(engine, std::format("emit('npc_hit', 'hero', '{}', 20, false)", mother));
    frames(engine, 3);
    CHECK(run(engine, std::format("boss_phase('{}')", mother)).asInteger() == 2);
    CHECK(living(engine, "SPAWN_E08_WELLE_2") == 2);
    CHECK(living(engine, "SPAWN_E08_WELLE_3") == 0);

    // Under a third: phase 3, the third.
    run(engine, std::format("npc_set_stat('{}', 'hp', 150)", mother));
    run(engine, std::format("emit('npc_hit', 'hero', '{}', 20, false)", mother));
    frames(engine, 3);
    CHECK(run(engine, std::format("boss_phase('{}')", mother)).asInteger() == 3);
    CHECK(living(engine, "SPAWN_E08_WELLE_3") == 2);
    CHECK(engine.scripts()->callErrors() == 0);
}

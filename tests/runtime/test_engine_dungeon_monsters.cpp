// Monsters of the dungeon in the engine (docs/modules/dungeon.md D6) on testworld/monsters.g7world: spawn
// markers put resting monsters in (zs_dormant), an event `wake` at a marker or the hero coming near wakes
// them, a niche wakes the mauerschlupfer that steals something shiny and loses it again, waking survives a
// save game, and only nests fill up again when the hero comes back into the level.

#include <g7/runtime/Engine.hpp>
#include <g7/save/GameState.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig monstersConfig()
{
    EngineConfig config;
    config.appName = "dungeon_monsters";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/monsters.g7world");
    config.start = "START_MONSTER";
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

std::string aiState(Engine& engine, std::string_view npc)
{
    return std::string(run(engine, std::format("npc_state('{}').state", npc)).asString());
}

bool dormant(Engine& engine, std::string_view npc)
{
    return run(engine, std::format("npc_dormant('{}')", npc)).asBool();
}

i64 spawned(Engine& engine, std::string_view spawn)
{
    return run(engine, std::format("#spawn_npcs('{}')", spawn)).asInteger();
}
} // namespace

TEST_CASE(
    "Engine dungeon monsters: spawn markers put resting monsters in; an event wakes them; a save keeps it")
{
    Engine engine(monstersConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    frames(engine, 3);

    CHECK(spawned(engine, "SPAWN_WACHE") == 1);
    CHECK(spawned(engine, "SPAWN_SCHLUPF") == 1);
    CHECK(spawned(engine, "SPAWN_NEST") == 2);
    CHECK(run(engine, "spawn_npcs('SPAWN_WACHE')[1]").asString() == "mon_gatterbock");
    CHECK(dormant(engine, "mon_gatterbock"));
    CHECK(aiState(engine, "mon_gatterbock") == "zs_dormant");
    frames(engine, 30);
    CHECK(run(engine, "npc_state('mon_gatterbock').animation").asString() == "dormant");

    // 20 m away he is beyond half its sight (25 m): it rests on.
    frames(engine, 120);
    CHECK(dormant(engine, "mon_gatterbock"));

    // The trigger sends `wake` to its marker.
    run(engine, "teleport(-15, 0, 0)");
    frames(engine, 10);
    CHECK_FALSE(dormant(engine, "mon_gatterbock"));
    const std::string woken = aiState(engine, "mon_gatterbock");
    CHECK((woken == "zs_mm_wake" || woken == "zs_mm_attack"));

    // A save keeps it awake and at its marker; the resting one rests on.
    auto saved = engine.captureGame();
    REQUIRE(saved.ok());
    REQUIRE(engine.applyGame(saved.value()).ok());
    frames(engine, 3);
    CHECK_FALSE(dormant(engine, "mon_gatterbock"));
    CHECK(spawned(engine, "SPAWN_WACHE") == 1);
    CHECK(aiState(engine, "mon_gatterbock") != "zs_dormant");
    CHECK(dormant(engine, "mon_mauerschlupfer"));
    CHECK(aiState(engine, "mon_mauerschlupfer") == "zs_dormant");
}

TEST_CASE("Engine dungeon monsters: a resting monster wakes when the hero comes within half its sight")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "teleport(0, 0, -10)"); // 10 m in front of it, it faces him
    frames(engine, 90);
    CHECK_FALSE(dormant(engine, "mon_gatterbock"));
}

TEST_CASE(
    "Engine dungeon monsters: a searched niche wakes the mauerschlupfer; it steals what glitters, drops it")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "give_item('it_ring_gold')");
    const i64 apples = run(engine, "give_item('it_apple', 3)").asInteger();
    run(engine, "teleport(15, 0, 1.5)");
    frames(engine, 2);
    const auto niche = engine.findMob("NISCHE");
    REQUIRE(niche.has_value());
    REQUIRE(engine.useMob(*niche).ok());
    bool stole = false;
    for (int i = 0; i < 60 * 15 && !stole; ++i)
    {
        REQUIRE(engine.runFrame());
        stole = run(engine, "item_count('it_ring_gold')").asInteger() == 0;
    }
    REQUIRE(stole);
    CHECK_FALSE(dormant(engine, "mon_mauerschlupfer"));
    CHECK(run(engine, "item_count('it_apple')").asInteger() == apples); // only what glitters
    CHECK(run(engine, "npc_item_count('mon_mauerschlupfer', 'it_ring_gold')").asInteger() == 1);
    CHECK(run(engine, "npc_state('mon_mauerschlupfer').hand").asString() == "it_ring_gold"); // in its mouth

    // Killed, it loses the ring: it lies in the world again.
    run(engine, "emit('npc_killed', 'mon_mauerschlupfer', 'hero')");
    CHECK(run(engine, "npc_item_count('mon_mauerschlupfer', 'it_ring_gold')").asInteger() == 0);
    bool lies = false;
    for (const WorldItemInfo& item : engine.worldItems())
    {
        lies = lies || item.instance == "it_ring_gold";
    }
    CHECK(lies);
}

TEST_CASE("Engine dungeon monsters: only a nest fills up again when the hero comes back into the level")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    // Both of the nest die (as a save game has them dead; the trap gate's and the fights' deaths are tested
    // elsewhere).
    auto state = engine.captureGame();
    REQUIRE(state.ok());
    u32 inNest = 0;
    for (save::NpcState& npc : state.value().worlds.back().npcs)
    {
        if (npc.spawn == "SPAWN_NEST")
        {
            npc.life = save::Life::Dead;
            ++inNest;
        }
    }
    CHECK(inNest == 2);
    REQUIRE(engine.applyGame(state.value()).ok());
    frames(engine, 3);
    CHECK(spawned(engine, "SPAWN_NEST") == 0);

    run(engine, "change_world('testworld/monsters_2.g7world', 'START_ZWEI')");
    frames(engine, 5);
    run(engine, "change_world('testworld/monsters.g7world', 'START_ZURUECK')");
    frames(engine, 5);
    CHECK(spawned(engine, "SPAWN_NEST") == 2);  // the nest grew again, resting
    CHECK(spawned(engine, "SPAWN_WACHE") == 1); // the others are as they were left
    CHECK(spawned(engine, "SPAWN_SCHLUPF") == 1);
    const script::Value nest = run(engine, "spawn_npcs('SPAWN_NEST')");
    REQUIRE(nest.asTable() != nullptr);
    for (const script::Value& npc : nest.asTable()->array)
    {
        CHECK(dormant(engine, npc.asString()));
    }
}

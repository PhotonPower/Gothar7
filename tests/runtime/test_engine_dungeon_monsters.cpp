// Monsters of the dungeon in the engine (docs/modules/dungeon.md D6) on testworld/monsters.g7world: spawn
// markers put resting monsters in (zs_dormant), an event `wake` at a marker or the hero coming near wakes
// them, a niche wakes the mauerschlupfer that steals something shiny and loses it again, waking survives a
// save game, and only nests fill up again when the hero comes back into the level.

#include <g7/gameplay/Movement.hpp>
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

TEST_CASE(
    "Engine dungeon monsters: the Mauerkriecher rests in its crack and comes out (t_emerge) at the hero")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    const std::string kriecher(run(engine, "insert_npc('mon_mauerkriecher', 'WP_MONSTER_OST')").asString());
    run(engine, std::format("npc_set_dormant('{}', true)", kriecher)); // as a spawn marker does (npc_spawned)
    run(engine, std::format("npc_start_state('{}', 'zs_dormant', 'WP_MONSTER_OST')", kriecher));
    frames(engine, 30);
    CHECK(dormant(engine, kriecher));
    CHECK(run(engine, std::format("npc_state('{}').animation", kriecher)).asString() == "dormant");
    run(engine, std::format("wake('{}')", kriecher)); // as when it notices the hero: out of the crack
    bool emerged = false;
    for (int i = 0; i < 180 && !emerged; ++i)
    {
        frames(engine, 1);
        emerged = run(engine, std::format("npc_state('{}').animation", kriecher)).asString() == "wake";
    }
    CHECK(emerged);
    CHECK_FALSE(dormant(engine, kriecher));
}

namespace
{
/// Puts `npc` `distance` m in front of the hero, turned `turn` degrees on the hero's yaw (0: facing away).
void placeAhead(Engine& engine, std::string_view npc, f32 distance, f32 turn)
{
    const auto get = [&](std::string_view key)
    { return static_cast<f32>(run(engine, std::format("where().{}", key)).asNumber()); };
    const Vec3 ahead =
        Vec3(get("x"), get("y"), get("z")) + gameplay::forwardOf(glm::radians(get("yaw"))) * distance;
    run(engine,
        std::format("npc_teleport('{}', {}, {}, {}, {})", npc, ahead.x, ahead.y, ahead.z, get("yaw") + turn));
}

std::string insertMother(Engine& engine)
{
    const std::string mother(run(engine, "insert_npc('mon_kellermutter', 'WP_MONSTER_OST')").asString());
    run(engine, std::format("npc_set_dormant('{}', true)", mother));
    run(engine, std::format("npc_start_state('{}', 'zs_dormant', 'WP_MONSTER_OST')", mother));
    frames(engine, 5);
    return mother;
}
} // namespace

TEST_CASE(
    "Engine dungeon monsters: the Kellermutter rages below two thirds and calls her brood (spawn_brood)")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "set_stat('hp_max', 5000) set_stat('hp', 5000)");
    const std::string mother = insertMother(engine);
    placeAhead(engine, mother, 8.0f, 180.0f);
    CHECK(run(engine, std::format("boss_phase('{}')", mother)).asInteger() == 1);
    run(engine, std::format("npc_set_stat('{}', 'hp', 380)", mother)); // under 2/3 of 600
    run(engine, std::format("emit('npc_hit', 'hero', '{}', 20, false)", mother));
    CHECK(run(engine, std::format("boss_phase('{}')", mother)).asInteger() == 2);
    bool raged = false;
    i64 brood = 0;
    for (int i = 0; i < 600 && brood < 3; ++i)
    {
        frames(engine, 1);
        raged = raged || run(engine, std::format("npc_state('{}').animation", mother)).asString() == "rage";
        brood = run(engine, std::format("brood_alive('{}')", mother)).asInteger();
    }
    CHECK(raged);
    CHECK(brood == 3); // Creatures.kellermutter.brood.count
    CHECK_FALSE(run(engine, std::format("npc_weak('{}')", mother)).asBool());
    // Under a third she goes to guard her hoard: the weak spot on the sac is open.
    run(engine, std::format("npc_set_stat('{}', 'hp', 150)", mother));
    run(engine, std::format("emit('npc_hit', 'hero', '{}', 20, false)", mother));
    CHECK(run(engine, std::format("boss_phase('{}')", mother)).asInteger() == 3);
    CHECK(run(engine, std::format("npc_weak('{}')", mother)).asBool());
    CHECK(aiState(engine, mother) == "zs_km_guard");
    CHECK(engine.scripts()->callErrors() == 0);
}

TEST_CASE(
    "Engine dungeon monsters: the Kellermutter's open weak spot takes double from behind and staggers her")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    engine.setRandomSource([] { return 0.99f; });
    frames(engine, 3);
    run(engine, "set_stat('hp_max', 5000) set_stat('hp', 5000) set_stat('str', 100)");
    run(engine, "give_item('it_sword_old') equip('it_sword_old') draw_weapon()");
    frames(engine, 110);
    run(engine, "on('npc_hit', function(a, t, d) if a == 'hero' then Story.dealt = d end end)");
    const std::string mother = insertMother(engine);
    run(engine, std::format("npc_set_stat('{}', 'hp_max', 3000)", mother)); // a weak-spot blow (~150) is 5 %
    run(engine, std::format("npc_set_stat('{}', 'hp', 3000)", mother));
    run(engine, std::format("npc_set_weak('{}', true)", mother));
    const auto strike = [&]
    {
        run(engine, "Story.dealt = nil");
        bool started = false;
        for (int i = 0; i < 180 && !started; ++i)
        {
            frames(engine, 1);
            started = run(engine, "hero_attack()").asBool();
        }
        REQUIRE(started);
        for (int i = 0; i < 60 && run(engine, "Story.dealt").isNil(); ++i)
        {
            frames(engine, 1);
        }
        REQUIRE_FALSE(run(engine, "Story.dealt").isNil());
        return run(engine, "Story.dealt").asInteger();
    };
    placeAhead(engine, mother, 3.1f, 0.0f); // her sac (behind her) towards the hero
    frames(engine, 2);
    const i64 back = strike();
    bool staggered = false;
    for (int i = 0; i < 30 && !staggered; ++i)
    {
        frames(engine, 1);
        staggered = run(engine, std::format("npc_state('{}').animation", mother)).asString() == "stagger";
    }
    placeAhead(engine, mother, 3.9f, 180.0f); // her armoured front
    frames(engine, 2);
    const i64 front = strike();
    MESSAGE("weak spot ", back, ", front ", front);
    CHECK(back >= 2 * front - 1);
    CHECK(staggered);
}

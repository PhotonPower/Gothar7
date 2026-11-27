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
/// A Kristallkrebs `distance` m in front of the hero, facing him, lying still (dormant) until told.
std::string crabInFront(Engine& engine, f32 distance)
{
    const std::string crab(run(engine, "insert_npc('mon_kristallkrebs', 'WP_MONSTER_OST')").asString());
    run(engine, std::format("npc_set_dormant('{}', true)", crab));
    run(engine, std::format("npc_start_state('{}', 'zs_dormant', 'WP_MONSTER_OST')", crab));
    frames(engine, 5);
    const auto get = [&](std::string_view key)
    { return static_cast<f32>(run(engine, std::format("where().{}", key)).asNumber()); };
    const Vec3 ahead =
        Vec3(get("x"), get("y"), get("z")) + gameplay::forwardOf(glm::radians(get("yaw"))) * distance;
    run(engine, std::format("npc_teleport('{}', {}, {}, {}, {})", crab, ahead.x, ahead.y, ahead.z,
                            get("yaw") + 180.0f));
    frames(engine, 5);
    return crab;
}

Vec3 heroAt(Engine& engine)
{
    const auto get = [&](std::string_view key)
    { return static_cast<f32>(run(engine, std::format("where().{}", key)).asNumber()); };
    return {get("x"), get("y"), get("z")};
}
} // namespace

TEST_CASE("Engine dungeon monsters: the Kristallkrebs's pincer holds the hero fast until it lets go")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    engine.setRandomSource([] { return 0.0f; }); // every attack of the crab is a grab (grab.chance)
    frames(engine, 3);
    run(engine, "set_stat('hp_max', 1000) set_stat('hp', 1000)");
    run(engine, "on('hero_grabbed', function(npc) Story.grabbed = npc end)");
    const std::string crab = crabInFront(engine, 1.6f);
    run(engine, std::format("wake('{}')", crab)); // it wakes and attacks the hero in front of it
    bool held = false;
    for (int i = 0; i < 400 && !held; ++i)
    {
        frames(engine, 1);
        held = run(engine, "hero_held()").isString();
    }
    REQUIRE(held);
    CHECK(run(engine, "hero_held()").asString() == crab);
    CHECK(run(engine, "Story.grabbed").asString() == crab);
    // Held, he does not get away walking.
    gameplay::MoveInput back;
    back.forward = -1.0f;
    engine.setPlayerInputOverride(back);
    const Vec3 before = heroAt(engine);
    frames(engine, 20);
    CHECK(glm::distance(before, heroAt(engine)) < 0.05f);
    // The pincer opens (event release, at most Creatures.kristallkrebs.grab.seconds).
    bool free = false;
    for (int i = 0; i < 200 && !free; ++i)
    {
        frames(engine, 1);
        free = run(engine, "hero_held()").isNil();
    }
    CHECK(free);
    engine.setPlayerInputOverride(std::nullopt);
}

TEST_CASE("Engine dungeon monsters: blunt blows knock crystals off the Kristallkrebs, up to splinter.max")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    engine.setRandomSource([] { return 0.99f; }); // no grabs
    frames(engine, 3);
    run(engine, "set_stat('hp_max', 1000) set_stat('hp', 1000) set_stat('str', 60)");
    run(engine, "give_item('it_club') equip('it_club') draw_weapon()");
    frames(engine, 110);
    const std::string crab = crabInFront(engine, 1.4f);
    run(engine, std::format("npc_set_stat('{}', 'hp_max', 5000)", crab));
    run(engine, std::format("npc_set_stat('{}', 'hp', 5000)", crab));
    const auto crystals = [&]
    { return run(engine, std::format("npc_item_count('{}', 'it_kristall')", crab)).asInteger(); };
    const auto strike = [&]
    {
        // Back in front of him (a hit crab steps about), then one blow with the club.
        const Vec3 at = heroAt(engine);
        const f32 yaw = static_cast<f32>(run(engine, "where().yaw").asNumber());
        const Vec3 ahead = at + gameplay::forwardOf(glm::radians(yaw)) * 1.4f;
        run(engine,
            std::format("npc_teleport('{}', {}, {}, {}, {})", crab, ahead.x, ahead.y, ahead.z, yaw + 180.0f));
        bool started = false;
        for (int i = 0; i < 180 && !started; ++i)
        {
            frames(engine, 1);
            started = run(engine, "hero_attack()").asBool();
        }
        REQUIRE(started);
        frames(engine, 40);
    };
    const i64 carried = crystals(); // its loot from the start (inventory)
    for (int i = 0; i < 5; ++i)
    {
        strike();
    }
    MESSAGE("crystals knocked off ", crystals() - carried);
    CHECK(crystals() - carried == 3); // at most splinter.max
}

TEST_CASE("Engine dungeon monsters: the Kristallkrebs throws half of the hero's spell back at him")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine,
        "give_item('it_rune_firebolt') equip('it_rune_firebolt') set_talent('magic_circle', 1) "
        "set_stat('mana_max', 50) set_stat('mana', 50) set_stat('hp_max', 1000) set_stat('hp', 1000)");
    run(engine,
        "on('npc_hit', function(a, t, d) if t == 'hero' then Story.back = (Story.back or 0) + d end end)");
    const std::string crab = crabInFront(engine, 5.0f);
    run(engine, std::format("npc_set_stat('{}', 'hp_max', 5000)", crab));
    run(engine, std::format("npc_set_stat('{}', 'hp', 5000)", crab));
    CHECK(run(engine, "draw_magic()").asString() == "it_rune_firebolt");
    frames(engine, 60);
    run(engine, "hero_cast(true)");
    frames(engine, 6);
    run(engine, "hero_cast(false)");
    bool back = false;
    for (int i = 0; i < 300 && !back; ++i)
    {
        frames(engine, 1);
        back = !run(engine, "Story.back").isNil();
    }
    CHECK(back);
    CHECK(run(engine, std::format("npc_stat('{}', 'hp')", crab)).asInteger() < 5000); // the rest reached it
}

TEST_CASE("Engine dungeon monsters: a heavy hit rolls the Mauerkriecher up - its shell takes most, not at "
          "its belly")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    const std::string kriecher(run(engine, "insert_npc('mon_mauerkriecher', 'WP_MONSTER_OST')").asString());
    run(engine, std::format("npc_set_dormant('{}', true)", kriecher)); // lies still until the first blow
    run(engine, std::format("npc_start_state('{}', 'zs_dormant', 'WP_MONSTER_OST')", kriecher));
    run(engine, std::format("npc_set_stat('{}', 'hp_max', 600)", kriecher));
    run(engine, std::format("npc_set_stat('{}', 'hp', 600)", kriecher));
    frames(engine, 10);
    // The hero strong (one blow is heavy: a quarter of its life or more), the Kriecher right in front of him.
    run(engine, "set_stat('str', 400)");
    run(engine, "set_stat('hp_max', 1000) set_stat('hp', 1000)"); // its bites do not knock him out
    run(engine, "give_item('it_sword_old') equip('it_sword_old')");
    run(engine, "draw_weapon()");
    frames(engine, 110);
    // `turn` degrees on the hero's yaw: 0 shows the hero its shell from behind, 180 its front (the belly).
    const auto putInFront = [&](f32 turn)
    {
        const auto get = [&](std::string_view key)
        { return static_cast<f32>(run(engine, std::format("where().{}", key)).asNumber()); };
        const Vec3 ahead =
            Vec3(get("x"), get("y"), get("z")) + gameplay::forwardOf(glm::radians(get("yaw"))) * 1.1f;
        run(engine, std::format("npc_teleport('{}', {}, {}, {}, {})", kriecher, ahead.x, ahead.y, ahead.z,
                                get("yaw") + turn));
    };
    putInFront(0.0f);
    frames(engine, 10);
    const auto hpNow = [&] { return run(engine, std::format("npc_stat('{}', 'hp')", kriecher)).asInteger(); };
    const auto animation = [&]
    { return std::string(run(engine, std::format("npc_state('{}').animation", kriecher)).asString()); };
    const i64 before = hpNow();
    REQUIRE(run(engine, "hero_attack()").asBool());
    bool curled = false;
    for (int i = 0; i < 120 && !curled; ++i)
    {
        frames(engine, 1);
        curled = animation() == "curl" || animation() == "curled";
    }
    const i64 first = before - hpNow();
    MESSAGE("first blow ", first);
    REQUIRE(curled);
    CHECK(first >= 150);
    // Rolled up it does not move; a second blow from behind mostly bounces off the shell.
    frames(engine, 40);
    const i64 middle = hpNow();
    REQUIRE(run(engine, "hero_attack()").asBool());
    frames(engine, 100);
    const i64 second = middle - hpNow();
    MESSAGE("second blow (rolled up) ", second);
    CHECK(second > 0);
    CHECK(second * 2 < first);
    CHECK((animation() == "curled" || animation() == "curl"));
    // After its time (4 s) it unrolls and goes on.
    bool unrolled = false;
    for (int i = 0; i < 300 && !unrolled; ++i)
    {
        frames(engine, 1);
        unrolled = animation() == "uncurl" || animation() == "move";
    }
    CHECK(unrolled);

    // Rolled up again, but facing the hero: the soft belly at its front takes the blow in full.
    run(engine, std::format("npc_set_stat('{}', 'hp_max', 1000)", kriecher)); // two full blows to take
    run(engine, std::format("npc_set_stat('{}', 'hp', 1000)", kriecher));
    putInFront(180.0f);
    frames(engine, 5);
    REQUIRE(run(engine, "hero_attack()").asBool());
    curled = false;
    for (int i = 0; i < 120 && !curled; ++i)
    {
        frames(engine, 1);
        curled = animation() == "curl" || animation() == "curled";
    }
    REQUIRE(curled);
    frames(engine, 40);
    putInFront(180.0f); // in case the blow pushed it round
    const i64 rolled = hpNow();
    REQUIRE(run(engine, "hero_attack()").asBool());
    frames(engine, 100);
    const i64 belly = rolled - hpNow();
    MESSAGE("blow on the belly (rolled up) ", belly);
    CHECK((animation() == "curled" || animation() == "curl"));
    CHECK(belly >= 2 * second);
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

namespace
{
/// What one blow of the hero (str 100, no critical hit) with `weapon` does to a resting `instance` facing him
/// `distance` m ahead.
i64 blowWith(std::string_view weapon, std::string_view instance, f32 distance)
{
    CAPTURE(weapon);
    CAPTURE(instance);
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    engine.setRandomSource([] { return 0.99f; });
    frames(engine, 3);
    run(engine, "set_stat('hp_max', 5000) set_stat('hp', 5000) set_stat('str', 100)");
    run(engine, std::format("give_item('{0}') equip('{0}') draw_weapon()", weapon));
    frames(engine, 110);
    run(engine, "on('npc_hit', function(a, t, d) if a == 'hero' then Story.dealt = d end end)");
    const std::string target(
        run(engine, std::format("insert_npc('{}', 'WP_MONSTER_OST')", instance)).asString());
    run(engine, std::format("npc_set_dormant('{}', true)", target));
    run(engine, std::format("npc_start_state('{}', 'zs_dormant', 'WP_MONSTER_OST')", target));
    run(engine, std::format("npc_set_stat('{0}', 'hp_max', 3000) npc_set_stat('{0}', 'hp', 3000)", target));
    frames(engine, 5);
    placeAhead(engine, target, distance, 180.0f);
    frames(engine, 2);
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
}
} // namespace

TEST_CASE(
    "Engine dungeon monsters: the Sensenklinge's bane does 50 more to the brood, nothing more to others")
{
    // Same target, old sword (edge 18, no bane) against the Sensenklinge (edge 32, bane brood +50).
    const i64 motherSword = blowWith("it_sword_old", "mon_kellermutter", 3.9f);
    const i64 motherBlade = blowWith("it_scythe_blade", "mon_kellermutter", 3.9f);
    CHECK(motherBlade - motherSword == (32 - 18) + 50);
    // Not of the brood (the Beinhauswart): only the better edge.
    const i64 wardenSword = blowWith("it_sword_old", "mon_beinhauswart", 1.6f);
    const i64 wardenBlade = blowWith("it_scythe_blade", "mon_beinhauswart", 1.6f);
    CHECK(wardenBlade - wardenSword == 32 - 18);
}

namespace
{
/// A Beinhauswart with its lantern, `distance` m in front of the hero (negative: behind him), facing him.
std::string wardenFacingHero(Engine& engine, f32 distance)
{
    const std::string warden(run(engine, "insert_npc('mon_beinhauswart', 'WP_MONSTER_OST')").asString());
    run(engine, std::format("npc_set_dormant('{}', true)", warden));
    run(engine, std::format("npc_start_state('{}', 'zs_dormant', 'WP_MONSTER_OST')", warden));
    run(engine, std::format("arm_warden('{}')", warden));
    frames(engine, 5);
    const auto get = [&](std::string_view key)
    { return static_cast<f32>(run(engine, std::format("where().{}", key)).asNumber()); };
    const Vec3 at =
        Vec3(get("x"), get("y"), get("z")) + gameplay::forwardOf(glm::radians(get("yaw"))) * distance;
    run(engine, std::format("npc_teleport('{}', {}, {}, {}, {})", warden, at.x, at.y, at.z,
                            get("yaw") + (distance >= 0.0f ? 180.0f : 0.0f)));
    frames(engine, 5);
    return warden;
}

/// Wakes it and waits up to `seconds` for its lantern to blind the hero.
bool blinded(Engine& engine, std::string_view warden, f32 seconds)
{
    run(engine, "Story.blinded = nil");
    run(engine, std::format("wake('{}')", warden));
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        frames(engine, 1);
        if (!run(engine, "Story.blinded").isNil())
        {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("Engine dungeon monsters: the Beinhauswart wakes and blinds the hero with its lantern")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "set_stat('hp_max', 1000) set_stat('hp', 1000)");
    run(engine, "on('hero_blinded', function(npc) Story.blinded = npc end)");
    const std::string warden = wardenFacingHero(engine, 4.0f);
    CHECK(run(engine, std::format("warden_has_lantern('{}')", warden)).asBool());
    REQUIRE(blinded(engine, warden, 6.0f));
    CHECK(run(engine, "Story.blinded").asString() == warden);
    CHECK(engine.screenFade() > 0.5f); // the screen is bright
    CHECK(run(engine, "fight_state('hero')").asString() == "stagger");
    frames(engine, 120);
    CHECK(engine.screenFade() == 0.0f); // and clears again
}

TEST_CASE("Engine dungeon monsters: the Beinhauswart's lantern does not blind a hero looking away")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "on('hero_blinded', function(npc) Story.blinded = npc end)");
    const std::string warden = wardenFacingHero(engine, -3.0f); // behind him
    CHECK_FALSE(blinded(engine, warden, 4.0f));
    CHECK(engine.screenFade() == 0.0f);
}

TEST_CASE("Engine dungeon monsters: the Beinhauswart drops its lantern at half its life and blinds no more")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "on('hero_blinded', function(npc) Story.blinded = npc end)");
    const std::string warden = wardenFacingHero(engine, 4.0f);
    run(engine, std::format("npc_set_stat('{}', 'hp', 50)", warden)); // of 110
    run(engine, std::format("emit('npc_hit', 'hero', '{}', 10, false)", warden));
    CHECK_FALSE(run(engine, std::format("warden_has_lantern('{}')", warden)).asBool());
    CHECK(run(engine, std::format("npc_item_count('{}', 'it_lantern_beinhaus')", warden)).asInteger() == 0);
    CHECK_FALSE(blinded(engine, warden, 4.0f));
}

TEST_CASE(
    "Engine dungeon monsters: the Brunnenmolch lurks, wakes, and sinks back at its water after the fight")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    const std::string newt(run(engine, "insert_npc('mon_brunnenmolch', 'WP_MONSTER_OST')").asString());
    run(engine, std::format("newt_water('{}', 'WP_MONSTER_OST')", newt));
    run(engine, std::format("npc_set_dormant('{}', true)", newt));
    run(engine, std::format("npc_start_state('{}', 'zs_dormant', 'WP_MONSTER_OST')", newt));
    frames(engine, 30);
    CHECK(run(engine, std::format("npc_state('{}').animation", newt)).asString() == "dormant");
    // Blind, it listens (t_listen) ...
    run(engine, std::format("npc_clear('{}') npc_play('{}', 'listen')", newt, newt));
    bool listened = false;
    for (int i = 0; i < 60 && !listened; ++i)
    {
        frames(engine, 1);
        listened = run(engine, std::format("npc_state('{}').animation", newt)).asString() == "listen";
    }
    CHECK(listened);
    // ... wakes (t_wake), and with nobody to fight goes back to its water and sinks under (t_submerge).
    run(engine, "teleport(-30, 0, -30)"); // far away: it cannot hear him
    run(engine, std::format("wake('{}')", newt));
    frames(engine, 10);
    CHECK_FALSE(dormant(engine, newt));
    bool submerged = false;
    bool resting = false;
    for (int i = 0; i < 1200 && !resting; ++i)
    {
        frames(engine, 1);
        const std::string anim(run(engine, std::format("npc_state('{}').animation", newt)).asString());
        submerged = submerged || anim == "submerge";
        resting = dormant(engine, newt) && anim == "dormant";
    }
    CHECK(submerged);
    CHECK(resting);
    // Its loot: skin and gills (figuren #475).
    CHECK(run(engine, std::format("npc_item_count('{}', 'it_newt_skin')", newt)).asInteger() == 1);
    CHECK(run(engine, std::format("npc_item_count('{}', 'it_newt_gills')", newt)).asInteger() == 2);
    CHECK(engine.scripts()->callErrors() == 0);
}

TEST_CASE("Engine dungeon monsters: the Kristallkrebs's crystals glow blue at socket_glow and move with it")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    const std::size_t before = engine.creatureLights().size();
    const std::string crab(run(engine, "insert_npc('mon_kristallkrebs', 'WP_MONSTER_OST')").asString());
    frames(engine, 5);
    const auto lights = engine.creatureLights();
    REQUIRE(lights.size() == before + 1);
    const render::PointLight& glow = lights.back();
    CHECK(glow.color.z > glow.color.x); // pale blue
    const f32 x = static_cast<f32>(run(engine, std::format("npc_state('{}').x", crab)).asNumber());
    const f32 y = static_cast<f32>(run(engine, std::format("npc_state('{}').y", crab)).asNumber());
    const f32 z = static_cast<f32>(run(engine, std::format("npc_state('{}').z", crab)).asNumber());
    CHECK(glm::distance(glow.position, Vec3(x, y, z)) < 1.5f); // on its back
    CHECK(glow.position.y > y + 0.3f);
    run(engine,
        std::format("npc_teleport('{}', {}, {}, {})", crab, x + 6.0f, y, z)); // it moves, the light too
    frames(engine, 3);
    CHECK(engine.creatureLights().back().position.x > glow.position.x + 5.0f);
}

TEST_CASE("Engine dungeon monsters: the Beinhauswart's lantern lights green while carried, not once dropped")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    const std::size_t before = engine.creatureLights().size();
    const std::string warden(run(engine, "insert_npc('mon_beinhauswart', 'WP_MONSTER_OST')").asString());
    frames(engine, 3);
    CHECK(engine.creatureLights().size() == before); // nothing in its hand yet
    run(engine, std::format("arm_warden('{}')", warden));
    frames(engine, 3);
    REQUIRE(engine.creatureLights().size() == before + 1);
    CHECK(engine.creatureLights().back().color.y > engine.creatureLights().back().color.x); // green
    run(engine, std::format("npc_set_stat('{}', 'hp', 40)", warden)); // under half: the lantern falls
    run(engine, std::format("emit('npc_hit', 'hero', '{}', 10, false)", warden));
    frames(engine, 3);
    CHECK(engine.creatureLights().size() == before);
}

TEST_CASE("Engine dungeon monsters: the Kellermutter sweeps her scythes in from the side (t_scythe_l/r)")
{
    Engine engine(monstersConfig());
    REQUIRE(engine.init().ok());
    engine.setRandomSource([] { return 0.0f; }); // every blow a side sweep (side_swing.chance)
    frames(engine, 3);
    run(engine, "set_stat('hp_max', 5000) set_stat('hp', 5000)");
    const std::string mother = insertMother(engine);
    placeAhead(engine, mother, 4.0f, 180.0f); // facing the hero
    run(engine, std::format("wake('{}')", mother));
    bool swept = false;
    for (int i = 0; i < 900 && !swept; ++i)
    {
        frames(engine, 1);
        const std::string anim(run(engine, std::format("npc_state('{}').animation", mother)).asString());
        swept = anim == "scythe_l" || anim == "scythe_r";
    }
    CHECK(swept);
    frames(engine, 60);
    CHECK(run(engine, "stat('hp')").asInteger() < 5000); // the sweep hits
}

TEST_CASE(
    "Engine dungeon monsters: on ebene_03 the Brunnenmolch snaps straight up at the hero on the basin edge")
{
    EngineConfig config = monstersConfig();
    config.world = fs::fromUtf8("worlds/leonberg/dungeon/ebene_03.g7world");
    config.start = "START_E03_VON_OBEN";
    Engine engine(config);
    REQUIRE(engine.init().ok());
    engine.setRandomSource([] { return 0.99f; });
    frames(engine, 5);
    run(engine, "set_stat('hp_max', 5000) set_stat('hp', 5000)");
    const script::Value names = run(engine, "spawn_npcs('SPAWN_E03_10_9')");
    REQUIRE(names.asTable() != nullptr);
    REQUIRE_FALSE(names.asTable()->array.empty());
    const std::string newt(names.asTable()->array[0].asString());
    const auto at = [&](std::string_view who, std::string_view key)
    {
        return static_cast<f32>(run(engine, who == "hero" ? std::format("where().{}", key)
                                                          : std::format("npc_state('{}').{}", who, key))
                                    .asNumber());
    };
    const Vec3 water(at(newt, "x"), at(newt, "y"), at(newt, "z"));
    // The hero on the edge (or the jetty) above it: the first spot around it at least 0.4 m higher.
    bool above = false;
    for (int i = 0; i < 64 && !above; ++i)
    {
        const f32 angle = glm::two_pi<f32>() * static_cast<f32>(i % 16) / 16.0f;
        const f32 radius = 1.5f + 1.5f * static_cast<f32>(i / 16); // 1.5, 3, 4.5, 6 m
        run(engine, std::format("teleport({}, {}, {})", water.x + radius * std::cos(angle), water.y + 1.5f,
                                water.z + radius * std::sin(angle)));
        frames(engine, 40); // lands
        above = at("hero", "y") - water.y > 0.4f;
    }
    REQUIRE(above);
    run(engine, std::format("wake('{}')", newt));
    bool snapped = false;
    for (int i = 0; i < 900 && !snapped; ++i)
    {
        frames(engine, 1);
        snapped = run(engine, std::format("npc_state('{}').animation", newt)).asString() == "snap_up";
    }
    CHECK(snapped); // straight up from the water instead of biting ahead
    CHECK(engine.scripts()->callErrors() == 0);
}

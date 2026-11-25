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

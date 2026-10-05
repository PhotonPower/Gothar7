// Combat (M11 part A, label "headless"): blows land within reach in the hit window and do the damage of
// Gothic 1 (K2), a parry from the front blocks (K6), people beaten by people fall unconscious and get up
// after 30 s, a blow on the one lying kills, animals die (K7), the hero gets up at the spot with little life
// (K8). The human combat clips are not there yet: the moves follow the fighter's timeline.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig combatConfig()
{
    EngineConfig config;
    config.appName = "combat";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

i64 hp(Engine& engine, std::string_view npc)
{
    return run(engine, std::format("npc_stat('{}', 'hp')", npc)).asInteger();
}

std::string state(Engine& engine, std::string_view who)
{
    return std::string(run(engine, std::format("fight_state('{}')", who)).asString());
}

/// Two NPCs 1.2 m apart on open ground, `a` looking at `b` (along -Z), `b` looking back; routines off.
void face(Engine& engine, std::string_view a, std::string_view b)
{
    for (const auto npc : {a, b})
    {
        if (!engine.runConsoleLine(std::format("npc_state('{}')", npc)).ok())
        {
            REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
        }
        run(engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    }
    run(engine, std::format("npc_teleport('{}', 40, 0, 20, 0)", a));
    run(engine, std::format("npc_teleport('{}', 40, 0, 18.8, 180)", b));
    runSeconds(engine, 0.2f);
}
} // namespace

TEST_CASE("Engine combat: a blow in reach does weapon + strength - protection; out of reach nothing")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(10, 0, 30)");
    run(engine, "on('npc_hit', function(a, t, d, c) Story.hit = string.format('%s>%s:%d', a, t, d) end)");
    face(engine, "npc_gate_guard", "npc_farmer_woman");
    const i64 before = hp(engine, "npc_farmer_woman");
    const i64 str = run(engine, "npc_stat('npc_gate_guard', 'str')").asInteger();
    // The guard's equipped weapon (edge) against the farmer woman without armour (no protection).
    REQUIRE(run(engine, "npc_attack('npc_gate_guard')").asBool());
    CHECK(state(engine, "npc_gate_guard") == "attack");
    runSeconds(engine, 1.0f);
    CHECK(state(engine, "npc_gate_guard") == "ready");
    const i64 damage = before - hp(engine, "npc_farmer_woman");
    CHECK(damage >= 5);
    CHECK(damage >= str); // strength at least
    CHECK(run(engine, "Story.hit").asString() == std::format("npc_gate_guard>npc_farmer_woman:{}", damage));

    // Out of reach: no hit.
    run(engine, "npc_teleport('npc_farmer_woman', 40, 0, 15, 180)");
    runSeconds(engine, 0.1f);
    const i64 far = hp(engine, "npc_farmer_woman");
    REQUIRE(run(engine, "npc_attack('npc_gate_guard')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(hp(engine, "npc_farmer_woman") == far);
}

TEST_CASE("Engine combat: a parry from the front blocks and the blow bounces off; from behind it does not")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(10, 0, 30)");
    run(engine, "on('npc_parried', function(d, a) Story.parried = d end)");
    // Two fighters with fists, of different guilds (comrades do not hit each other): farmer woman and old
    // man.
    face(engine, "npc_farmer_woman", "npc_old_man");
    const i64 before = hp(engine, "npc_old_man");
    REQUIRE(run(engine, "npc_attack('npc_farmer_woman')").asBool());
    runSeconds(engine, 0.1f);
    REQUIRE(run(engine, "npc_parry('npc_old_man')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(hp(engine, "npc_old_man") == before);
    CHECK(run(engine, "Story.parried").asString() == "npc_old_man");
    // Turned away: the parry does not help.
    run(engine, "npc_teleport('npc_old_man', 40, 0, 18.8, 0)");
    runSeconds(engine, 0.1f);
    REQUIRE(run(engine, "npc_attack('npc_farmer_woman')").asBool());
    runSeconds(engine, 0.1f);
    REQUIRE(run(engine, "npc_parry('npc_old_man')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(hp(engine, "npc_old_man") < before);
}

TEST_CASE(
    "Engine combat: beaten people fall unconscious and get up after 30 s; a blow on the one lying kills")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(10, 0, 30)");
    run(engine, "on('npc_knocked_out', function(t, a) Story.ko = t end)");
    run(engine, "on('npc_killed', function(t, a) Story.killed = t end)");
    face(engine, "npc_gate_guard", "npc_old_man");
    run(engine, "npc_set_stat('npc_old_man', 'hp', 3)");
    REQUIRE(run(engine, "npc_attack('npc_gate_guard')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(state(engine, "npc_old_man") == "down");
    CHECK(hp(engine, "npc_old_man") == 1);
    CHECK(run(engine, "Story.ko").asString() == "npc_old_man");
    runSeconds(engine, 25.0f);
    CHECK(state(engine, "npc_old_man") == "down");
    runSeconds(engine, 6.0f);
    CHECK(state(engine, "npc_old_man") == "ready"); // K7: 30 s

    // Down again, then the blow on the one lying.
    run(engine, "npc_teleport('npc_old_man', 40, 0, 18.8, 180)");
    run(engine, "npc_set_stat('npc_old_man', 'hp', 3)");
    REQUIRE(run(engine, "npc_attack('npc_gate_guard')").asBool());
    runSeconds(engine, 1.0f);
    REQUIRE(state(engine, "npc_old_man") == "down");
    REQUIRE(run(engine, "npc_attack('npc_gate_guard')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(state(engine, "npc_old_man") == "dead");
    CHECK(run(engine, "Story.killed").asString() == "npc_old_man");
}

TEST_CASE("Engine combat: animals die; the hero falls and gets up at the spot with little life")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(10, 0, 30)");
    run(engine, "on('npc_killed', function(t, a) Story.killed = t end)");
    face(engine, "npc_gate_guard", "mon_wolf");
    run(engine, "npc_set_stat('mon_wolf', 'hp', 3)");
    REQUIRE(run(engine, "npc_attack('npc_gate_guard')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(state(engine, "mon_wolf") == "dead"); // K7: animals are not knocked out
    CHECK(run(engine, "Story.killed").asString() == "mon_wolf");

    // The hero in front of the guard, 2 hp left: down, then up with a tenth of his life (K8).
    run(engine, "set_stat('hp', 2)");
    run(engine, "teleport(40, 0, 18.8)");
    runSeconds(engine, 0.2f);
    REQUIRE(run(engine, "npc_attack('npc_gate_guard')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(state(engine, "hero") == "down");
    runSeconds(engine, 5.5f);
    CHECK(state(engine, "hero") == "ready");
    CHECK(run(engine, "stat('hp')").asInteger() == (run(engine, "stat('hp_max')").asInteger() + 5) / 10);
}

TEST_CASE("Engine combat: with the weapon drawn the hero locks the nearest enemy ahead and turns to it (K5)")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    face(engine, "npc_farmer_woman", "npc_woodcutter"); // both at x 40, z 20 / 18.8
    // The hero 4 m from the farmer woman, looking past her (along -Z, she is ahead to the right).
    run(engine, "teleport(42, 0, 23)");
    runSeconds(engine, 0.2f);
    CHECK_FALSE(engine.heroCombatTarget().has_value()); // no weapon drawn: no lock
    run(engine, "draw_weapon()");
    runSeconds(engine, 1.0f);
    REQUIRE(engine.heroCombatTarget().has_value());
    CHECK(*engine.heroCombatTarget() == "npc_farmer_woman"); // the nearest ahead
    // Turned to her: a blow now hits her.
    const i64 before = hp(engine, "npc_farmer_woman");
    run(engine, "npc_teleport('npc_farmer_woman', 42, 0, 22)");
    runSeconds(engine, 0.5f);
    REQUIRE(run(engine, "hero_attack()").asBool());
    runSeconds(engine, 1.0f);
    CHECK(hp(engine, "npc_farmer_woman") < before);
    // Sheathed: the lock is gone.
    run(engine, "draw_weapon()");
    runSeconds(engine, 1.0f);
    CHECK_FALSE(engine.heroCombatTarget().has_value());
}

TEST_CASE(
    "Engine combat: knocked out by the hero - worse attitude, witnesses angry, looting the one lying (K7)")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    // The farmer woman in front of the hero; the gate guard (friendly to farmers) behind him, watching.
    face(engine, "npc_old_man", "npc_farmer_woman");
    run(engine, "npc_teleport('npc_old_man', 30, 0, 30, 0)"); // out of the way
    run(engine, "npc_teleport('npc_farmer_woman', 40, 0, 18.8, 0)");
    if (!engine.runConsoleLine("npc_state('npc_gate_guard')").ok())
    {
        REQUIRE(run(engine, "insert_npc('npc_gate_guard', 'wp_camp_center')").isString());
    }
    run(engine, "set_routine('npc_gate_guard', '') npc_clear('npc_gate_guard')");
    run(engine, "npc_teleport('npc_gate_guard', 45, 0, 18.8, 90)"); // looking along -X at the scene
    run(engine, "teleport(41.2, 0, 18.8)"); // the hero starts looking along -X: she is ahead
    runSeconds(engine, 0.5f);
    const std::string before(run(engine, "npc_attitude('npc_farmer_woman')").asString());
    run(engine, "npc_set_stat('npc_farmer_woman', 'hp', 2)");
    run(engine, "draw_weapon()");
    runSeconds(engine, 1.0f);
    REQUIRE(run(engine, "hero_attack()").asBool());
    runSeconds(engine, 1.0f);
    REQUIRE(state(engine, "npc_farmer_woman") == "down");
    // One step worse, for good (Story).
    CHECK(run(engine, "npc_attitude('npc_farmer_woman')").asString() != before);
    CHECK(run(engine, "Story.attitudes.npc_farmer_woman").isString());
    CHECK(run(engine, "npc_attitude('npc_gate_guard')").asString() == "angry"); // he saw it

    // Looting: only the one lying, only near.
    run(engine, "npc_give_item('npc_farmer_woman', 'it_apple', 3)");
    const i64 apples = run(engine, "npc_item_count('npc_farmer_woman', 'it_apple')").asInteger();
    CHECK(run(engine, "loot('npc_farmer_woman', 'it_apple')").asInteger() == apples); // all of them
    CHECK(run(engine, "npc_item_count('npc_farmer_woman', 'it_apple')").asInteger() == 0);
    CHECK_FALSE(engine.runConsoleLine("loot('npc_gate_guard', 'it_sword_old')").ok()); // standing
    run(engine, "teleport(50, 0, 20)");
    runSeconds(engine, 0.2f);
    run(engine, "npc_give_item('npc_farmer_woman', 'it_bread', 1)");
    CHECK_FALSE(engine.runConsoleLine("loot('npc_farmer_woman', 'it_bread')").ok()); // too far
}

TEST_CASE("Engine combat AI: an attacked NPC fights back; it leaves the hero lying (K8)")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    face(engine, "npc_old_man", "npc_woodcutter");
    run(engine, "npc_teleport('npc_old_man', 30, 0, 30, 0)");
    // The hero strikes the woodcutter: he fights back until the hero lies.
    run(engine, "npc_teleport('npc_woodcutter', 40, 0, 18.8, 270)"); // looking along +X at the hero
    run(engine, "teleport(41.2, 0, 18.8)");
    run(engine, "set_stat('hp', 40)");
    run(engine, "draw_weapon()");
    runSeconds(engine, 1.0f);
    REQUIRE(run(engine, "hero_attack()").asBool());
    runSeconds(engine, 1.5f);
    CHECK(run(engine, "npc_state('npc_woodcutter').state").asString() == "zs_attack");
    bool down = false;
    for (int i = 0; i < 60 && !down; ++i)
    {
        runSeconds(engine, 0.5f);
        down = state(engine, "hero") == "down";
    }
    REQUIRE(down); // the hero does not fight back here
    runSeconds(engine, 1.5f);
    CHECK(run(engine, "npc_state('npc_woodcutter').state").asString() != "zs_attack"); // leaves him lying
}

TEST_CASE("Engine combat AI: a pack attacks two at a time, a wounded wolf flees, the hunted bird dies")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(-41, 0, -26)");                         // 8 m from the wolf den
    run(engine, "set_stat('hp_max', 1000) set_stat('hp', 1000)"); // the hero lasts
    runSeconds(engine, 0.5f);
    REQUIRE(run(engine, "#insert_pack('mon_wolf', 3, 'wp_wolf_den')").asInteger() == 3);
    for (const char* wolf : {"mon_wolf", "mon_wolf#2", "mon_wolf#3"})
    {
        run(engine, std::format("set_routine('{}', '') npc_clear('{}')", wolf, wolf));
        run(engine, std::format("fight('{}', 'hero')", wolf));
    }
    i64 most = 0;
    for (int i = 0; i < 40; ++i)
    {
        runSeconds(engine, 0.5f);
        most = std::max(most, run(engine, "fight_attackers('hero')").asInteger());
    }
    CHECK(most == 2); // the third waits
    CHECK(run(engine, "stat('hp')").asInteger() < 1000);

    // Below a fifth of its life a wolf runs off.
    run(engine, "npc_set_stat('mon_wolf', 'hp', 5)");
    runSeconds(engine, 1.5f);
    CHECK(run(engine, "npc_state('mon_wolf').state").asString() != "zs_mm_attack");
    CHECK(run(engine, "npc_state('mon_wolf').state").asString() != "zs_attack");

    // A wolf hunting a bird kills it (the hero out of the way: near him the wolves would threaten him).
    run(engine, "teleport(-35, 0, 5)");
    runSeconds(engine, 1.0f);
    REQUIRE(run(engine, "insert_npc('mon_laufvogel', 'wp_wolf_den')").isString());
    run(engine, "set_routine('mon_laufvogel', '') npc_clear('mon_laufvogel') npc_set_stat('mon_laufvogel', "
                "'hp', 10)");
    run(engine, "npc_set_stat('mon_wolf#2', 'hp', 60)");
    run(engine, "fight('mon_wolf#2', 'mon_laufvogel')");
    bool killed = false;
    for (int i = 0; i < 40 && !killed; ++i)
    {
        runSeconds(engine, 0.5f);
        killed = state(engine, "mon_laufvogel") == "dead";
    }
    CHECK(killed);
}

TEST_CASE(
    "Engine ranged: the bow shoots the focused target by talent, reloads, needs arrows; ranged kills (R1-R4)")
{
    Engine engine(combatConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine,
        "set_stat('dex', 20) give_item('it_bow_short') equip('it_bow_short') give_item('it_arrow', 3) "
        "set_talent('bow', 2)");
    // R1: no melee weapon equipped - the draw key takes the bow.
    run(engine, "teleport(41.2, 0, 18.8)"); // looking along -X
    runSeconds(engine, 0.3f);
    CHECK(run(engine, "draw_weapon()").asString() == "ranged");
    run(engine, "draw_ranged()"); // and away again
    CHECK(run(engine, "player_weapon()").asString() == "none");
    // An old man 12 m ahead (west), the bow drawn: locked.
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_old_man', '') npc_clear('npc_old_man') npc_teleport('npc_old_man', 29.2, "
                "0, 18.8, 270)");
    CHECK(run(engine, "draw_ranged()").asString() == "ranged");
    runSeconds(engine, 0.5f);
    REQUIRE(engine.heroCombatTarget().has_value());
    const i64 before = hp(engine, "npc_old_man");
    engine.setRandomSource([] { return 0.0f; }); // R4: the roll hits
    REQUIRE(run(engine, "hero_shoot()").asBool());
    CHECK_FALSE(run(engine, "hero_shoot()").asBool()); // reloading (R2)
    runSeconds(engine, 1.0f);
    CHECK(hp(engine, "npc_old_man") == before - 15); // bow point 15, no protection (R3)
    CHECK(run(engine, "npc_item_count('npc_old_man', 'it_arrow')").asInteger() == 1); // stuck in him
    // A missed roll goes 5 degrees aside: no hit.
    engine.setRandomSource([] { return 0.99f; });
    REQUIRE(run(engine, "hero_shoot()").asBool());
    runSeconds(engine, 1.5f);
    CHECK(hp(engine, "npc_old_man") == before - 15);
    // The last arrow kills him (ranged combat kills, K7).
    run(engine, "npc_set_stat('npc_old_man', 'hp', 5)");
    engine.setRandomSource([] { return 0.0f; });
    runSeconds(engine, 0.5f);
    REQUIRE(run(engine, "hero_shoot()").asBool());
    runSeconds(engine, 1.5f);
    CHECK(state(engine, "npc_old_man") == "dead");
    // Out of arrows.
    CHECK(run(engine, "item_count('it_arrow')").asInteger() == 0);
    CHECK_FALSE(run(engine, "hero_shoot()").asBool());
}

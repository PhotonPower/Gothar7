// Weapon sweep (M11, label "headless", gameplay.md "Kampf" - "Trefferprüfung"): with Combat.weapon_sweep on,
// the weapon at socket_hand_r is swept along the attack clip in the hit window - a target in its arc is hit
// once per swing, one behind the attacker is not, a door between them stops the blow; without the sweep a
// door blocks the blow by reach and angle too. A hit that staggers pushes the target back (knockback), a
// parry the attacker.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig sweepConfig()
{
    EngineConfig config;
    config.appName = "sweep";
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

/// The guard (sword) and the farmer woman, routines off; hits counted in Story.hits.
void setUp(Engine& engine, bool sweep)
{
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(10, 0, 30)");
    run(engine, std::format("Combat.weapon_sweep = {} combat_reload()", sweep ? "true" : "false"));
    run(engine, "Story.hits = 0 on('npc_hit', function(a, t) if a == 'npc_gate_guard' then Story.hits = "
                "Story.hits + 1 end end)");
    for (const auto npc : {"npc_gate_guard", "npc_farmer_woman"})
    {
        if (!engine.runConsoleLine(std::format("npc_state('{}')", npc)).ok())
        {
            REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
        }
        run(engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    }
    run(engine,
        "npc_set_stat('npc_farmer_woman', 'hp_max', 1000) npc_set_stat('npc_farmer_woman', 'hp', 1000)");
    // She counts as fighting someone else already, so she does not hit back (ai/combat.lua, npc_hit).
    run(engine, "Fights['npc_farmer_woman'] = 'nobody'");
}

/// One blow of the guard (his whole clip); the hits it did.
i64 blow(Engine& engine)
{
    const i64 before = run(engine, "Story.hits").asInteger();
    REQUIRE(run(engine, "npc_attack('npc_gate_guard')").asBool());
    runSeconds(engine, 1.8f);
    return run(engine, "Story.hits").asInteger() - before;
}
} // namespace

TEST_CASE("Weapon sweep: the blade along the clip hits a target in its arc once, not one behind the attacker")
{
    Engine engine(sweepConfig());
    REQUIRE(engine.init().ok());
    setUp(engine, true);
    // The guard looks along -Z at the farmer woman 1.2 m ahead: one swing, one hit (not one per step).
    run(engine, "npc_teleport('npc_gate_guard', 40, 0, 20, 0)");
    run(engine, "npc_teleport('npc_farmer_woman', 40, 0, 18.8, 180)");
    runSeconds(engine, 0.2f);
    CHECK(blow(engine) == 1);
    CHECK(blow(engine) == 1);
    // Behind him the blade does not come.
    run(engine, "npc_teleport('npc_farmer_woman', 40, 0, 21.2, 0)");
    run(engine, "npc_teleport('npc_gate_guard', 40, 0, 20, 0)");
    runSeconds(engine, 0.2f);
    CHECK(blow(engine) == 0);
    // Far ahead neither.
    run(engine, "npc_teleport('npc_farmer_woman', 40, 0, 16.5, 180)");
    run(engine, "npc_teleport('npc_gate_guard', 40, 0, 20, 0)");
    runSeconds(engine, 0.2f);
    CHECK(blow(engine) == 0);
}

TEST_CASE("Weapon sweep: a closed door between the fighters stops the blow - with the sweep and without")
{
    // LAGER_TUER_ZU: a closed door standing alone at z -9, from x 33.1 to 34.0.
    for (const bool sweep : {true, false})
    {
        CAPTURE(sweep);
        Engine engine(sweepConfig());
        REQUIRE(engine.init().ok());
        setUp(engine, sweep);
        run(engine, "npc_teleport('npc_gate_guard', 33.55, 0, -8.35, 0)");
        run(engine, "npc_teleport('npc_farmer_woman', 33.55, 0, -9.65, 180)");
        runSeconds(engine, 0.2f);
        CHECK(blow(engine) == 0);
        // Beside the door the same blow lands.
        run(engine, "npc_teleport('npc_gate_guard', 36, 0, -8.35, 0)");
        run(engine, "npc_teleport('npc_farmer_woman', 36, 0, -9.65, 180)");
        runSeconds(engine, 0.2f);
        CHECK(blow(engine) == 1);
    }
}

TEST_CASE("Weapon sweep: a hit that staggers pushes the target back along the blow")
{
    Engine engine(sweepConfig());
    REQUIRE(engine.init().ok());
    setUp(engine, false);
    run(engine,
        "npc_set_stat('npc_farmer_woman', 'hp_max', 100) npc_set_stat('npc_farmer_woman', 'hp', 100)");
    run(engine, "npc_teleport('npc_gate_guard', 40, 0, 20, 0)");
    run(engine, "npc_teleport('npc_farmer_woman', 40, 0, 18.8, 180)");
    runSeconds(engine, 0.2f);
    const f64 before = run(engine, "npc_distance('npc_gate_guard', 'npc_farmer_woman')").asNumber();
    REQUIRE(blow(engine) == 1);
    const f64 after = run(engine, "npc_distance('npc_gate_guard', 'npc_farmer_woman')").asNumber();
    CHECK(after - before > 0.2); // Combat.knockback 0.3 m
    CHECK(after - before < 0.45);
    // Without knockback she stays.
    run(engine, "Combat.knockback = 0 combat_reload()");
    const f64 still = run(engine, "npc_distance('npc_gate_guard', 'npc_farmer_woman')").asNumber();
    REQUIRE(blow(engine) == 1);
    CHECK(run(engine, "npc_distance('npc_gate_guard', 'npc_farmer_woman')").asNumber() ==
          doctest::Approx(still).epsilon(0.02));
}

TEST_CASE("Weapon sweep: a parried attacker is pushed back a little")
{
    Engine engine(sweepConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(10, 0, 30)");
    run(engine, "Story.parried = nil on('npc_parried', function(d, a) Story.parried = a end)");
    // Fists against fists (fists parry no weapons), of different guilds.
    for (const auto npc : {"npc_farmer_woman", "npc_old_man"})
    {
        if (!engine.runConsoleLine(std::format("npc_state('{}')", npc)).ok())
        {
            REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
        }
        run(engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    }
    run(engine, "npc_teleport('npc_farmer_woman', 40, 0, 20, 0)");
    run(engine, "npc_teleport('npc_old_man', 40, 0, 18.8, 180)");
    runSeconds(engine, 0.2f);
    const f64 before = run(engine, "npc_distance('npc_farmer_woman', 'npc_old_man')").asNumber();
    REQUIRE(run(engine, "npc_attack('npc_farmer_woman')").asBool());
    runSeconds(engine, 0.1f);
    REQUIRE(run(engine, "npc_parry('npc_old_man')").asBool());
    runSeconds(engine, 1.8f);
    REQUIRE(run(engine, "Story.parried").asString() == "npc_farmer_woman");
    const f64 after = run(engine, "npc_distance('npc_farmer_woman', 'npc_old_man')").asNumber();
    CHECK(after - before > 0.12); // Combat.parry_knockback 0.2 m
    CHECK(after - before < 0.3);
}

TEST_CASE("Weapon sweep: the combat AI lands blows with its fists (the fists swept, fist_reach matched)")
{
    Engine engine(sweepConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    run(engine, "teleport(10, 0, 30)");
    run(engine, "Combat.weapon_sweep = true Combat.fist_sweep = true combat_reload()");
    run(engine, "Story.fists = 0 on('npc_hit', function(a, t) if a == 'npc_old_man' then Story.fists = "
                "Story.fists + 1 "
                "end end)");
    for (const auto npc : {"npc_farmer_woman", "npc_old_man"})
    {
        if (!engine.runConsoleLine(std::format("npc_state('{}')", npc)).ok())
        {
            REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
        }
        run(engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    }
    run(engine,
        "npc_set_stat('npc_farmer_woman', 'hp_max', 1000) npc_set_stat('npc_farmer_woman', 'hp', 1000)");
    run(engine, "Fights['npc_farmer_woman'] = 'nobody'"); // she does not hit back
    // 3 m apart: the old man walks up to his fists' reach and punches.
    run(engine, "npc_teleport('npc_old_man', 40, 0, 20, 0)");
    run(engine, "npc_teleport('npc_farmer_woman', 40, 0, 17, 180)");
    runSeconds(engine, 0.2f);
    run(engine, "fight('npc_old_man', 'npc_farmer_woman')");
    runSeconds(engine, 15.0f);
    CHECK(run(engine, "Story.fists").asInteger() >= 6); // 13 now; with the old fist_reach 0.9 only 2
}

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
    // Two fighters with fists: the gate guard's weapon taken away, the farmer woman unarmed.
    face(engine, "npc_farmer_woman", "npc_woodcutter");
    const i64 before = hp(engine, "npc_woodcutter");
    REQUIRE(run(engine, "npc_attack('npc_farmer_woman')").asBool());
    runSeconds(engine, 0.1f);
    REQUIRE(run(engine, "npc_parry('npc_woodcutter')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(hp(engine, "npc_woodcutter") == before);
    CHECK(run(engine, "Story.parried").asString() == "npc_woodcutter");
    // Turned away: the parry does not help.
    run(engine, "npc_teleport('npc_woodcutter', 40, 0, 18.8, 0)");
    runSeconds(engine, 0.1f);
    REQUIRE(run(engine, "npc_attack('npc_farmer_woman')").asBool());
    runSeconds(engine, 0.1f);
    REQUIRE(run(engine, "npc_parry('npc_woodcutter')").asBool());
    runSeconds(engine, 1.0f);
    CHECK(hp(engine, "npc_woodcutter") < before);
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

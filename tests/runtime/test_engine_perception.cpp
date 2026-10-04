// NPC perception (M9 part C, label "headless"): sight with cone, range and line of sight, shorter when
// sneaking and at night; the hero drawing his weapon and the guard's warnings; a private area; noises.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig perceptionConfig()
{
    EngineConfig config;
    config.appName = "perception";
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

std::string stateOf(Engine& engine, std::string_view npc)
{
    return std::string(run(engine, std::format("npc_state('{}').state", npc)).asString());
}

/// An NPC standing still at `at` (no routine), turned towards the player at `player`.
void placeFacingPlayer(Engine& engine, std::string_view npc, std::string_view at, const Vec3& player)
{
    REQUIRE(run(engine, std::format("insert_npc('{}', '{}')", npc, at)).asBool());
    run(engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    run(engine, std::format("teleport({}, {}, {})", player.x, player.y, player.z));
    runSeconds(engine, 0.5f);
    run(engine, std::format("npc_turn_to_player('{}')", npc));
    runSeconds(engine, 1.5f);
}
} // namespace

TEST_CASE("Engine perception: sight cone, range, sneaking and night")
{
    Engine engine(perceptionConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    // The guard on the camp's centre (7, 0, -4), the player 10 m east of him.
    placeFacingPlayer(engine, "npc_gate_guard", "wp_camp_center", Vec3(17.0f, 0.0f, -4.0f));
    CHECK(run(engine, "npc_sees_player('npc_gate_guard')").asBool());
    CHECK(run(engine, "npc_distance_to_player('npc_gate_guard')").asNumber() ==
          doctest::Approx(10.0).epsilon(0.1));

    // Behind him: outside the cone.
    run(engine, "teleport(-3, 0, -4)");
    runSeconds(engine, 0.5f);
    CHECK_FALSE(run(engine, "npc_sees_player('npc_gate_guard')").asBool());

    // 18 m in front: seen by day; not when sneaking (half the range) or at night (0.6).
    run(engine, "teleport(25, 0, -4)");
    runSeconds(engine, 0.5f);
    CHECK(run(engine, "npc_sees_player('npc_gate_guard')").asBool());
    gameplay::MoveInput sneak;
    sneak.sneak = true;
    engine.setPlayerInputOverride(sneak);
    runSeconds(engine, 0.2f);
    CHECK_FALSE(run(engine, "npc_sees_player('npc_gate_guard')").asBool());
    engine.setPlayerInputOverride(std::nullopt);
    run(engine, "time(23, 0)");
    runSeconds(engine, 0.2f);
    CHECK_FALSE(run(engine, "npc_sees_player('npc_gate_guard')").asBool());
}

TEST_CASE("Engine perception: a drawn weapon, the guard warns, then would attack")
{
    Engine engine(perceptionConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('npc_would_attack', function(npc, reason) Story.attack = npc .. ' ' .. reason end)");
    placeFacingPlayer(engine, "npc_gate_guard", "wp_camp_center", Vec3(12.0f, 0.0f, -4.0f));

    // Fists without a weapon; a sword in the hand with one.
    engine.toggleWeapon();
    CHECK(engine.weaponMode() == 2);
    CHECK(run(engine, "player_weapon()").asString() == "fists");
    runSeconds(engine, 0.5f); // he looks five times a second
    CHECK(stateOf(engine, "npc_gate_guard") == "zs_warn_weapon");
    // Put away: he calms down (after finishing his line).
    engine.toggleWeapon();
    CHECK(engine.weaponMode() == 0);
    runSeconds(engine, 3.5f);
    CHECK(stateOf(engine, "npc_gate_guard").empty());

    // Drawn again and kept: each warning, then 4 s; after the second he would attack (and threatens,
    // following).
    runSeconds(engine, 2.0f); // his "calm" line
    run(engine, "give_item('it_sword_old') equip('it_sword_old')");
    engine.toggleWeapon();
    CHECK(engine.weaponMode() == 1);
    CHECK(run(engine, "player_weapon()").asString() == "weapon");
    runSeconds(engine, 15.0f);
    CHECK(run(engine, "Story.attack").asString() == "npc_gate_guard weapon");
    CHECK(stateOf(engine, "npc_gate_guard") == "zs_threaten");
}

TEST_CASE("Engine perception: the owner notices the player in his private area")
{
    Engine engine(perceptionConfig());
    REQUIRE(engine.init().ok());
    // The guard at the gate (21.5, 0, 0); his sleeping corner TRG_PRIVAT_WACHE east of it.
    placeFacingPlayer(engine, "npc_gate_guard", "wp_camp_gate", Vec3(27.0f, 0.0f, -4.0f));
    CHECK(run(engine, "player_inside('TRG_PRIVAT_WACHE')").asBool());
    CHECK(stateOf(engine, "npc_gate_guard") == "zs_intruder");
    CHECK_FALSE(engine.runConsoleLine("player_inside('TRG_NOWHERE')").ok());
    // Out again: done.
    run(engine, "teleport(34, 0, 3)");
    runSeconds(engine, 1.5f);
    CHECK_FALSE(run(engine, "player_inside('TRG_PRIVAT_WACHE')").asBool());
    CHECK(stateOf(engine, "npc_gate_guard").empty());
}

TEST_CASE("Engine perception: noises are heard within their radius")
{
    Engine engine(perceptionConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_south')").asBool());
    run(engine, "set_routine('npc_old_man', '') npc_clear('npc_old_man')");
    run(engine, "on('assess_noise', function(npc, kind) Story.heard = npc .. ' ' .. kind end)");
    runSeconds(engine, 1.0f);
    run(engine, "noise(40, 0, 40, 5, 'far')"); // too far away
    runSeconds(engine, 1.2f);
    CHECK(run(engine, "Story.heard").isNil());
    run(engine, "noise(7, 0, 8, 5, 'bang')");
    runSeconds(engine, 1.2f);
    CHECK(run(engine, "Story.heard").asString() == "npc_old_man bang");
    CHECK(stateOf(engine, "npc_old_man") == "zs_look_around");
}

TEST_CASE("Engine attitudes: temporary ones are forgotten, permanent ones are saved")
{
    Engine engine(perceptionConfig());
    REQUIRE(engine.init().ok());
    CHECK(run(engine, "npc_attitude('npc_gate_guard')").asString() == "neutral"); // the hero has no guild yet
    run(engine, "set_temp_attitude('npc_gate_guard', 'angry', 2)");
    CHECK(run(engine, "npc_attitude('npc_gate_guard')").asString() == "angry");
    runSeconds(engine, 2.5f);
    CHECK(run(engine, "npc_attitude('npc_gate_guard')").asString() == "neutral");
    run(engine, "set_attitude('npc_old_man', 'friendly')");
    CHECK(run(engine, "npc_attitude('npc_old_man')").asString() == "friendly");
    CHECK(run(engine, "Story.attitudes.npc_old_man").asString() == "friendly"); // saved with the game
    CHECK_FALSE(engine.runConsoleLine("set_attitude('npc_old_man', 'grumpy')").ok());
}

TEST_CASE("Engine attitudes: a hostile guard would attack, his friends come to help, the weak run away")
{
    Engine engine(perceptionConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('npc_would_attack', function(npc, reason) Story.attack = npc .. ' ' .. reason end)");
    run(engine, "set_attitude('npc_gate_guard', 'hostile')");
    // The guard in the centre (7, 0, -4) looking north; the woodcutter (farmer, friends of the guards) 10 m
    // east, the old man (outcast, level 1) 6 m north-west. The player far away first.
    run(engine, "teleport(60, 0, 60)");
    for (const auto& [npc, at] :
         {std::pair{"npc_gate_guard", "wp_camp_center"}, std::pair{"npc_woodcutter", "wp_camp_east"},
          std::pair{"npc_old_man", "wp_camp_north"}})
    {
        REQUIRE(run(engine, std::format("insert_npc('{}', '{}')", npc, at)).asBool());
        run(engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    }
    runSeconds(engine, 1.0f);
    // The player appears 7 m in front of the guard.
    run(engine, "teleport(7, 0, -11)");
    runSeconds(engine, 1.0f);
    CHECK(run(engine, "Story.attack").asString() == "npc_gate_guard hostile");
    CHECK(stateOf(engine, "npc_gate_guard") == "zs_threaten");
    CHECK(stateOf(engine, "npc_woodcutter") == "zs_threaten"); // came to help
    CHECK(run(engine, "npc_attitude('npc_woodcutter')").asString() == "angry");
    CHECK(stateOf(engine, "npc_old_man") == "zs_flee");
    const f64 before = run(engine, "npc_distance_to_player('npc_old_man')").asNumber();
    runSeconds(engine, 4.0f);
    CHECK(run(engine, "npc_distance_to_player('npc_old_man')").asNumber() > before + 4.0);
}

TEST_CASE("Engine weapon: drawn and put away with figuren's clips, the sword appears at their events")
{
    Engine engine(perceptionConfig());
    REQUIRE(engine.init().ok());
    run(engine, "give_item('it_sword_old') equip('it_sword_old')");
    runSeconds(engine, 0.5f);
    CHECK(run(engine, "draw_weapon()").asString() == "weapon");
    runSeconds(engine, 0.1f);
    CHECK_FALSE(engine.playerHolds("socket_hand_r")); // the hand is still on the way to the hilt
    runSeconds(engine, 1.5f);
    CHECK(engine.playerHolds("socket_hand_r"));
    CHECK(run(engine, "draw_weapon()").asString() == "none");
    runSeconds(engine, 0.1f);
    CHECK(engine.playerHolds("socket_hand_r")); // until the clip puts it away
    runSeconds(engine, 1.5f);
    CHECK_FALSE(engine.playerHolds("socket_hand_r"));
}

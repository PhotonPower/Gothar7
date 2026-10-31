// NPCs using mobs (plan approved 2026-10-08, Gothic's AI_UseMob; label "headless"): NPCs sit at the camp's
// table on different slots, the hero sits down beside them where one is free (owner) and finds no room at a
// full table; walking away they stand up and free their seat; sleepers take their own bed first (owner), then
// a free one nearby, then the ground.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig mobConfig()
{
    EngineConfig config;
    config.appName = "npc_mobs";
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

std::string mobOf(Engine& engine, std::string_view npc)
{
    return std::string(run(engine, std::format("npc_state('{}').mob", npc)).asString());
}

Vec3 positionOf(Engine& engine, std::string_view npc)
{
    const auto c = [&](char axis)
    { return static_cast<f32>(run(engine, std::format("npc_state('{}').{}", npc, axis)).asNumber()); };
    return Vec3(c('x'), c('y'), c('z'));
}

/// Inserts `npc` without a routine at `x, z`.
void place(Engine& engine, std::string_view npc, f32 x, f32 z)
{
    REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
    run(engine, std::format("set_routine('{}', '') npc_clear('{}') npc_teleport('{}', {}, 0, {}, 0)", npc,
                            npc, npc, x, z));
}

/// Runs until every NPC listed sits (or `limit` seconds).
void waitSeated(Engine& engine, std::initializer_list<const char*> npcs, std::string_view mob, f32 limit)
{
    for (f32 t = 0.0f; t < limit; t += 0.5f)
    {
        bool all = true;
        for (const char* npc : npcs)
        {
            all = all && mobOf(engine, npc) == mob;
        }
        if (all)
        {
            return;
        }
        runSeconds(engine, 0.5f);
    }
}
} // namespace

TEST_CASE("Engine NPC mobs: NPCs share the table's slots with the hero, stand up when they walk on")
{
    Engine engine(mobConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true teleport(30, 0, 30)"); // the hero aside
    const char* sitters[] = {"npc_farmer_woman", "npc_woodcutter", "npc_old_man"};
    for (int i = 0; i < 3; ++i)
    {
        place(engine, sitters[i], 11.0f + static_cast<f32>(i) * 0.8f, 11.0f); // south of the table
    }
    run(engine,
        "npc_use_mob('npc_farmer_woman', 'table', 15) npc_use_mob('npc_woodcutter', 'table', 15, 'talk')");
    waitSeated(engine, {"npc_farmer_woman", "npc_woodcutter"}, "table", 20.0f);
    REQUIRE(mobOf(engine, "npc_farmer_woman") == "table");
    REQUIRE(mobOf(engine, "npc_woodcutter") == "table");
    const Vec3 a = positionOf(engine, "npc_farmer_woman");
    const Vec3 b = positionOf(engine, "npc_woodcutter");
    CHECK(glm::length(a - b) > 0.5f);                               // two slots
    CHECK(glm::length(Vec2(a.x, a.z) - Vec2(11.7f, 14.5f)) < 1.5f); // at the table
    CHECK(run(engine, "npc_state('npc_woodcutter').animation").asString() ==
          "table_talk"); // the loop variant
    CHECK(run(engine, "npc_state('npc_farmer_woman').animation").asString() == "table_loop");

    // The hero sits down at a free slot beside them (owner decision 2).
    const auto table = engine.findMob("LAGER_TISCH");
    REQUIRE(table.has_value());
    run(engine, "teleport(11.7, 0, 17)");
    runSeconds(engine, 0.3f);
    REQUIRE(engine.useMob(*table).ok());
    runSeconds(engine, 3.0f);
    CHECK(engine.mobPhase().value_or("") == "loop");
    // The old man takes the last slot; a fourth NPC finds none.
    run(engine, "npc_use_mob('npc_old_man', 'table', 15)");
    waitSeated(engine, {"npc_old_man"}, "table", 15.0f);
    CHECK(mobOf(engine, "npc_old_man") == "table");
    place(engine, "npc_camp_hexer", 12.5f, 11.2f);
    run(engine, "npc_use_mob('npc_camp_hexer', 'table', 15)");
    runSeconds(engine, 2.0f);
    CHECK(mobOf(engine, "npc_camp_hexer").empty()); // full: the command ends at once
    CHECK(run(engine, "npc_state('npc_camp_hexer').commands").asInteger() == 0);

    // Walking on: the woman stands up first (table_leave), her seat is free again for the hexer.
    run(engine, "npc_goto('npc_farmer_woman', 'wp_camp_center')");
    runSeconds(engine, 0.3f);
    CHECK(run(engine, "npc_state('npc_farmer_woman').animation").asString() == "table_leave");
    runSeconds(engine, 3.0f);
    CHECK(mobOf(engine, "npc_farmer_woman").empty());
    run(engine, "npc_use_mob('npc_camp_hexer', 'table', 15)");
    waitSeated(engine, {"npc_camp_hexer"}, "table", 15.0f);
    CHECK(mobOf(engine, "npc_camp_hexer") == "table");
}

TEST_CASE("Engine NPC mobs: sleepers take their own bed first, then a free one nearby, then the ground")
{
    Engine engine(mobConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true teleport(30, 0, 30) time(23, 0)");
    for (const char* npc : {"npc_gate_guard", "npc_woodcutter", "npc_old_man"})
    {
        place(engine, npc, 28.5f, -1.0f); // east of the fence, by the guard's sleeping place
    }
    // All three sleep at the guard's sleeping place; the nearer bed (LAGER_BETT) is nobody's, the other one
    // (LAGER_WACHE_BETT) the guard's.
    run(engine, "npc_start_state('npc_gate_guard', 'zs_sleep', 'wp_camp_guard_bed')");
    waitSeated(engine, {"npc_gate_guard"}, "bed", 20.0f);
    run(engine, "npc_start_state('npc_woodcutter', 'zs_sleep', 'wp_camp_guard_bed')");
    waitSeated(engine, {"npc_woodcutter"}, "bed", 20.0f);
    run(engine, "npc_start_state('npc_old_man', 'zs_sleep', 'wp_camp_guard_bed')");
    for (int i = 0; i < 40 && run(engine, "npc_state('npc_old_man').ambient").asString() != "sleep_ground";
         ++i)
    {
        runSeconds(engine, 0.5f);
    }

    CHECK(mobOf(engine, "npc_gate_guard") == "bed");
    CHECK(glm::length(Vec2(positionOf(engine, "npc_gate_guard").x, positionOf(engine, "npc_gate_guard").z) -
                      Vec2(27.3f, -6.05f)) < 0.6f);  // his own bed's slot
    CHECK(mobOf(engine, "npc_woodcutter") == "bed"); // the free one
    CHECK(mobOf(engine, "npc_old_man").empty());     // no bed left: on the ground
    CHECK(run(engine, "npc_state('npc_old_man').ambient").asString() == "sleep_ground");
    // Lying people stay (the state's loop does not restart them).
    runSeconds(engine, 8.0f);
    CHECK(mobOf(engine, "npc_gate_guard") == "bed");
    CHECK(run(engine, "npc_state('npc_gate_guard').state").asString() == "zs_sleep");

    // The morning: he gets up and his bed is free.
    run(engine, "npc_start_state('npc_gate_guard', 'zs_stand_guarding', 'wp_camp_gate')");
    runSeconds(engine, 6.0f);
    CHECK(mobOf(engine, "npc_gate_guard").empty());
    run(engine, "npc_use_mob('npc_old_man', 'bed', 12)");
    waitSeated(engine, {"npc_old_man"}, "bed", 15.0f);
    CHECK(mobOf(engine, "npc_old_man") == "bed");
}

TEST_CASE("Engine NPC mobs: the hammer is in the hand at the anvil from t_start's item_to_hand to t_stop's "
          "item_from_hand")
{
    Engine engine(mobConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true teleport(5, 0, 12)"); // the hero aside
    place(engine, "npc_woodcutter", 31.0f, -0.5f);                 // beside the anvil (slot at 29.6, -1.5)
    CHECK(run(engine, "npc_state('npc_woodcutter').hand").asString().empty());
    run(engine, "npc_use_mob('npc_woodcutter', 'anvil', 15)");
    waitSeated(engine, {"npc_woodcutter"}, "anvil", 20.0f);
    REQUIRE(mobOf(engine, "npc_woodcutter") == "anvil");
    CHECK(run(engine, "npc_state('npc_woodcutter').animation").asString() == "anvil_loop");
    CHECK(run(engine, "npc_state('npc_woodcutter').hand").asString() == "it_hammer");
    // Leaving: t_stop puts it away at its event (frame 11), not at once.
    run(engine, "npc_leave_mob('npc_woodcutter')");
    runSeconds(engine, 0.1f);
    CHECK(run(engine, "npc_state('npc_woodcutter').animation").asString() == "anvil_leave");
    CHECK(run(engine, "npc_state('npc_woodcutter').hand").asString() == "it_hammer");
    runSeconds(engine, 3.0f);
    CHECK(mobOf(engine, "npc_woodcutter").empty());
    CHECK(run(engine, "npc_state('npc_woodcutter').hand").asString().empty());
}

TEST_CASE(
    "Engine NPC states: church, beggar and lute (figuren's church.glb, music.glb), tools at the freepoints")
{
    Engine engine(mobConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true teleport(30, 0, 30)"); // the hero aside
    // The camp has no FP_MUSIC, FP_PRAY ...: the state plays at its way point.
    place(engine, "npc_leo_musician", 7.0f, -4.0f);
    const auto state = [&](const char* field)
    { return std::string(run(engine, std::format("npc_state('npc_leo_musician').{}", field)).asString()); };
    run(engine, "npc_start_state('npc_leo_musician', 'zs_play_lute', 'wp_camp_center')");
    runSeconds(engine, 3.0f);
    CHECK(state("ambient") == "play_lute");
    CHECK(state("animation") == "amb_play_lute");
    CHECK(state("hand") == "it_lute"); // taken at item_to_hand of t_play_lute_in
    CHECK(
        glm::length(Vec2(positionOf(engine, "npc_leo_musician").x, positionOf(engine, "npc_leo_musician").z) -
                    Vec2(7.0f, -4.0f)) < 1.0f);
    // Another state: t_play_lute_out puts the lute away, then he kneels to pray.
    for (const auto& [zs, loop] :
         {std::pair{"zs_pray", "amb_pray"}, std::pair{"zs_pray_stand", "amb_pray_stand"},
          std::pair{"zs_preach", "amb_preach"}, std::pair{"zs_beg", "amb_beg"}})
    {
        CAPTURE(zs);
        run(engine, std::format("npc_start_state('npc_leo_musician', '{}', 'wp_camp_center')", zs));
        runSeconds(engine, 4.0f);
        CHECK(state("animation") == loop);
        CHECK(state("hand").empty());
    }
    // The joiner's saw and the tailor's shears at their REPAIR freepoint (no _in clip: in the hand at once).
    run(engine, "npc_start_state('npc_leo_musician', 'zs_repair_saw', 'wp_camp_center')");
    runSeconds(engine, 3.0f);
    CHECK(state("animation") == "amb_repair_kneel");
    CHECK(state("hand") == "it_saw");
    run(engine, "npc_start_state('npc_leo_musician', 'zs_repair_shears', 'wp_camp_center')");
    runSeconds(engine, 3.0f);
    CHECK(state("hand") == "it_shears");
}

TEST_CASE(
    "Engine NPC mobs: the musician sits down on the bench with the lute and stands up with it (bench_lute)")
{
    Engine engine(mobConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true teleport(30, 0, 30)"); // the hero aside
    place(engine, "npc_leo_musician", 17.5f, -3.5f); // in front of the camp's bench (17.5, -5.5)
    run(engine, "npc_use_mob('npc_leo_musician', 'bench', 15, 'lute')");
    for (f32 t = 0.0f; t < 10.0f; t += 0.1f)
    {
        if (run(engine, "npc_state('npc_leo_musician').animation").asString() == "bench_lute_enter")
        {
            break;
        }
        runSeconds(engine, 0.1f);
    }
    CHECK(run(engine, "npc_state('npc_leo_musician').animation").asString() == "bench_lute_enter"); // its own
    waitSeated(engine, {"npc_leo_musician"}, "bench", 10.0f);
    REQUIRE(mobOf(engine, "npc_leo_musician") == "bench");
    CHECK(run(engine, "npc_state('npc_leo_musician').animation").asString() == "bench_lute");
    CHECK(run(engine, "npc_state('npc_leo_musician').hand").asString() == "it_lute");
    // Seated already, another loop: the lute goes; back to the lute: in the hand at once.
    run(engine, "npc_use_mob('npc_leo_musician', 'bench', 15, 'talk')");
    runSeconds(engine, 0.5f);
    CHECK(run(engine, "npc_state('npc_leo_musician').animation").asString() == "bench_talk");
    CHECK(run(engine, "npc_state('npc_leo_musician').hand").asString().empty());
    run(engine, "npc_use_mob('npc_leo_musician', 'bench', 15, 'lute')");
    runSeconds(engine, 0.5f);
    CHECK(run(engine, "npc_state('npc_leo_musician').hand").asString() == "it_lute");
    // Standing up its own way; the lute stays until item_from_hand of t_play_lute_sit_out (frame 18).
    run(engine, "npc_leave_mob('npc_leo_musician')");
    runSeconds(engine, 0.1f);
    CHECK(run(engine, "npc_state('npc_leo_musician').animation").asString() == "bench_lute_leave");
    CHECK(run(engine, "npc_state('npc_leo_musician').hand").asString() == "it_lute");
    runSeconds(engine, 3.0f);
    CHECK(mobOf(engine, "npc_leo_musician").empty());
    CHECK(run(engine, "npc_state('npc_leo_musician').hand").asString().empty());
}

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

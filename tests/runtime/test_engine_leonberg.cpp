// Leonberg (label "headless", with the generated town; not in CI: generated files are not versioned): the
// camera indoors and outdoors, doors that open inwards. The residents are in
// test_engine_leonberg_residents.cpp.

#include <g7/ai/Waynet.hpp>
#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/WorldFile.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <ostream> // doctest needs it to print std::string operands
#include <sstream>
#include <string>

using namespace g7;

namespace
{
const std::string kWorld = std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/leonberg.g7world";
const std::string kGenerated =
    std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/generated/leonberg_terrain.r16";

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}
} // namespace

TEST_CASE(
    "Leonberg: the camera's indoor profile in the inn, the outside one on the market (with the generated "
    "town)")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.fixedFrameSeconds = 1.0 / 60.0;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    for (int i = 0; i < 120; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.playerIndoorBlend() < 0.05f); // the market square
    const auto inn = engine.waynet().find("WP_LEO_GASTHAUS_ZHE_INNEN");
    REQUIRE(inn.has_value());
    const Vec3 p = engine.waynet().points()[*inn].position;
    run(engine, std::format("teleport({}, {}, {})", p.x, p.y + 0.1f, p.z));
    for (int i = 0; i < 180; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.playerIndoorBlend() > 0.95f);
    CHECK(engine.playerCameraDistance() <= 1.95f);
}

TEST_CASE(
    "Leonberg: out through an inward door - waiting outside the leaf's arc, not pushed beside the hinge "
    "(with the generated town)")
{
    // welt #227: house Zl2-T2, its door opens into the house. In, the leaf swings away; out, towards the NPC.
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.fixedFrameSeconds = 1.0 / 60.0;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    run(engine, "teleport(-18.3, 0, 18.5)"); // near by: simulated
    run(engine, "on('npc_arrived', function(npc, target) if npc == 'npc_leo_citizen' then Story.at = target "
                "end end)");
    run(engine, "on('npc_blocked', function(npc, target) if npc == 'npc_leo_citizen' then Story.at = "
                "'blocked' end end)");
    REQUIRE(run(engine, "insert_npc('npc_leo_citizen', 'WP_LEO_WOHNHAUS_ZL2_T2')").isString());
    run(engine, "set_routine('npc_leo_citizen', '') npc_clear('npc_leo_citizen')");
    const auto walk = [&](std::string_view goal)
    {
        run(engine, "Story.at = nil");
        run(engine, std::format("npc_goto('npc_leo_citizen', '{}')", goal));
        for (int i = 0; i < 60 * 40 && run(engine, "Story.at").isNil(); ++i)
        {
            REQUIRE(engine.runFrame());
        }
        return std::string(run(engine, "tostring(Story.at)").asString());
    };
    CHECK(walk("WP_LEO_WOHNHAUS_ZL2_T2_KAMMER") == "WP_LEO_WOHNHAUS_ZL2_T2_KAMMER");
    CHECK(walk("WP_LEO_WOHNHAUS_ZL2_T2") == "WP_LEO_WOHNHAUS_ZL2_T2");
}

TEST_CASE("Leonberg: the priest sleeps in the bed of the parish house's parlour, the monks in the chamber's "
          "(with the generated town)")
{
    // welt #326: the parish house ZiU is enterable - one bed in the parlour (INNEN), two in the chamber
    // (KAMMER).
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.startTime = "22:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    const auto door = engine.waynet().find("WP_LEO_PFARRHAUS_ZIU");
    REQUIRE(door.has_value());
    const Vec3 p = engine.waynet().points()[*door].position;
    run(engine, std::format("teleport({}, {}, {})", p.x, p.y + 0.1f, p.z)); // near by: simulated
    for (int i = 0; i < 60 * 60; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    const auto room = [&](std::string_view npc, std::string_view wp)
    {
        const auto at = engine.waynet().find(wp);
        REQUIRE(at.has_value());
        const Vec3 w = engine.waynet().points()[*at].position;
        const f32 x = static_cast<f32>(run(engine, std::format("npc_state('{}').x", npc)).asNumber());
        const f32 z = static_cast<f32>(run(engine, std::format("npc_state('{}').z", npc)).asNumber());
        return glm::length(Vec2(x - w.x, z - w.z));
    };
    for (const char* npc : {"npc_leo_priest", "npc_leo_monk_1", "npc_leo_monk_2"})
    {
        CHECK_MESSAGE(run(engine, std::format("npc_state('{}').mob", npc)).asString() == "bed", npc);
    }
    // The parlour's bed is about 6 m from its way point, the chamber's beds 2-3 m from theirs.
    CHECK(room("npc_leo_priest", "WP_LEO_PFARRHAUS_ZIU_INNEN") < 7.0f);
    CHECK(room("npc_leo_priest", "WP_LEO_PFARRHAUS_ZIU_KAMMER") > 4.0f);
    CHECK(room("npc_leo_monk_1", "WP_LEO_PFARRHAUS_ZIU_KAMMER") < 3.5f);
    CHECK(room("npc_leo_monk_2", "WP_LEO_PFARRHAUS_ZIU_KAMMER") < 3.5f);
}

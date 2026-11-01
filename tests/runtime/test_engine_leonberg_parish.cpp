// Leonberg's parish house (label "headless", with the generated town; not in CI: generated files are not
// versioned): priest and monks sleep in their beds (welt #326). Its own file and ctest entry for the run
// time.

#include <g7/ai/Waynet.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
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
    for (int i = 0; i < 25 * 60; ++i)                                       // 25 s: to the bed and into it
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

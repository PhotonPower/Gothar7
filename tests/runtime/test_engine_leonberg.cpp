// Leonberg (label "headless", with the generated town; not in CI: generated files are not versioned): the
// camera indoors and outdoors. Doors that open inwards: test_engine_leonberg_doors.cpp; the residents:
// test_engine_leonberg_residents*.cpp (own ctest entries for their run time).

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

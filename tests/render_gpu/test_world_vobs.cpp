// Vob types in the engine (label "gpu"): start points place the camera, triggers notice it, the debug
// overlay draws the invisible vobs.

#include "GlFixture.hpp"

#include <g7/runtime/Engine.hpp>
#include <g7/world/Components.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

using namespace g7;

namespace
{
EngineConfig worldConfig(std::string start = {})
{
    EngineConfig config;
    config.appName = "world_vobs";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.start = std::move(start);
    config.player = false; // these tests drive the camera (the player: test_player.cpp)
    g7::test::keepVideoAlive();
    return config;
}
} // namespace

TEST_CASE("World vobs GPU: the camera starts on the start point, triggers notice it")
{
    Engine engine(worldConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    // START_LAGER: feet at (34, 0, 0), looking west (-X).
    const Vec3 eye = engine.camera().transform.position;
    CHECK(eye.x == doctest::Approx(34.0f));
    CHECK(eye.y == doctest::Approx(world::kStartEyeHeight));
    const Vec3 forward = engine.camera().transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    CHECK(forward.x == doctest::Approx(-1.0f).epsilon(0.001));

    const world::VobId gate{172}; // TRG_CAMP_GATE, box around (24, 1.5, 0)
    CHECK(engine.runFrame());
    CHECK_FALSE(engine.triggers().isInside(gate, Engine::kCameraProbe));
    engine.camera().transform.position = Vec3(24.5f, 1.7f, 1.0f);
    CHECK(engine.runFrame());
    CHECK(engine.triggers().isInside(gate, Engine::kCameraProbe));

    // The overlay draws start points, triggers, sounds and mobs without GL errors.
    engine.setDebugOverlay(true);
    CHECK(engine.runFrame());
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("World vobs GPU: --start picks a start point by name; an unknown one fails")
{
    {
        Engine engine(worldConfig("start_lager"));
        REQUIRE(engine.init().ok());
        CHECK(engine.camera().transform.position.x == doctest::Approx(34.0f));
    }
    Engine engine(worldConfig("START_NOWHERE"));
    auto result = engine.init();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message == "cannot load world: unknown start point 'START_NOWHERE' (the world has: "
                                    "START_LAGER, START_LAGER_HOEHLE, START_KLETTERPLATZ, START_TEICH)");
}

TEST_CASE("World vobs GPU: small deco vanishes by size, gameplay (the mob) never; distance hides all")
{
    Engine engine(worldConfig());
    REQUIRE(engine.init().ok());
    // Look at the mob STOOL_CAMPFIRE (3, 0, 3) from 6 m east of it; the fly camera keeps the start point's
    // direction (west, -X) and only the position is set here.
    engine.camera().transform.position = Vec3(9.0f, 1.7f, 3.0f);
    engine.cullSettings() = {.viewDistance = 0.0f, .sizeCull = 0.0f};
    CHECK(engine.runFrame());
    const u32 all = engine.visibleSceneObjects();
    CHECK(all > 3);
    CHECK(engine.culledBySize() == 0);

    // Absurdly strict size threshold: every deco object goes, the mob stays.
    engine.cullSettings().sizeCull = 100.0f;
    CHECK(engine.runFrame());
    CHECK(engine.culledBySize() > 0);
    CHECK(engine.visibleSceneObjects() >= 1);
    CHECK(engine.visibleSceneObjects() < all);

    // A view distance of 1 m hides the mob too (gameplay objects obey the distance).
    engine.cullSettings() = {.viewDistance = 1.0f, .sizeCull = 0.0f};
    CHECK(engine.runFrame());
    CHECK(engine.culledByDistance() > 0);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("World vobs GPU: level change to the cave and back keeps the camp as it was left")
{
    Engine engine(worldConfig("START_LAGER_HOEHLE"));
    REQUIRE(engine.init().ok());
    CHECK(engine.worldPath() == "testworld/camp.g7world");
    const usize campModels = engine.loadedModels();

    // Move the mob STOOL_CAMPFIRE before leaving: the camp keeps it there.
    world::Scene& camp = engine.scene();
    const entt::entity stool = camp.findById(world::VobId{68});
    Transform moved = *std::as_const(camp).get<Transform>(stool);
    moved.position = Vec3(5.0f, 0.0f, 5.0f);
    camp.setTransform(stool, moved);

    // Standing next to TRG_TO_CAVE (x -31.5 .. -28.5) does nothing; stepping in changes the world.
    CHECK(engine.runFrame());
    CHECK(engine.worldPath() == "testworld/camp.g7world");
    engine.camera().transform.position = Vec3(-30.0f, 1.7f, 20.0f);
    CHECK(engine.runFrame());
    CHECK(engine.worldPath() == "testworld/cave.g7world");
    CHECK(engine.camera().transform.position.z == doctest::Approx(5.0f)); // START_HOEHLE
    CHECK(engine.keptWorlds() == 1);
    CHECK(engine.loadedModels() < campModels); // camp models the cave does not use are released
    CHECK(engine.runFrame());
    CHECK(engine.worldPath() == "testworld/cave.g7world"); // the start lies outside TRG_TO_CAMP

    // Back through TRG_TO_CAMP (z 7.5 .. 9.5): arrival at START_LAGER_HOEHLE, the stool where it was put.
    engine.camera().transform.position = Vec3(0.0f, 1.7f, 8.5f);
    CHECK(engine.runFrame());
    CHECK(engine.worldPath() == "testworld/camp.g7world");
    CHECK(engine.camera().transform.position.x == doctest::Approx(-26.0f));
    const entt::entity again = engine.scene().findById(world::VobId{68});
    REQUIRE(engine.scene().valid(again));
    CHECK(std::as_const(engine.scene()).get<Transform>(again)->position == Vec3(5.0f, 0.0f, 5.0f));
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("World vobs GPU: no bouncing back from a start inside a level change; unknown targets stay")
{
    // A world whose start lies inside a level change back to the camp.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path dir = std::filesystem::temp_directory_path() / ("g7_pingpong_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    REQUIRE(fs::writeText(dir / "pingpong.g7world", R"({"version": 1, "vobs": [
      {"id": 1, "type": "start", "name": "START_IN_TRIGGER", "pos": [0, 0, 0]},
      {"id": 2, "type": "trigger", "name": "TRG_BACK", "pos": [0, 1, 0], "components": {"trigger": {
        "halfExtents": [2, 2, 2], "changeWorld": {"world": "testworld/camp.g7world", "start": "START_LAGER"}}}},
      {"id": 3, "type": "mesh", "name": "ROCK", "pos": [0, 0, -8], "mesh": "testscene/nature/rock_largeA.glb"}]})")
                .ok());
    EngineConfig config = worldConfig();
    config.world = dir / "pingpong.g7world";
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    const std::string here = engine.worldPath();
    for (int i = 0; i < 5; ++i)
    {
        CHECK(engine.runFrame()); // standing in TRG_BACK since arrival: no change
    }
    CHECK(engine.worldPath() == here);

    engine.requestWorldChange("testworld/nowhere.g7world", "START_LAGER");
    CHECK(engine.runFrame());
    CHECK(engine.worldPath() == here);
    engine.requestWorldChange("testworld/camp.g7world", "START_NOWHERE");
    CHECK(engine.runFrame());
    CHECK(engine.worldPath() == here);

    // Out of the trigger and back in: now it changes.
    engine.camera().transform.position = Vec3(0.0f, 1.7f, 6.0f);
    CHECK(engine.runFrame());
    engine.camera().transform.position = Vec3(0.0f, 1.7f, 0.0f);
    CHECK(engine.runFrame());
    CHECK(engine.worldPath() == "testworld/camp.g7world");
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("World vobs GPU: day and night follow the game time, which survives a level change")
{
    EngineConfig config = worldConfig("START_LAGER_HOEHLE");
    config.startTime = "12:00";
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    CHECK(engine.runFrame());
    CHECK(engine.gameTime().hourOfDay() == doctest::Approx(12.0f).epsilon(0.01));
    CHECK(engine.environment().sunIntensity > 1.0f);
    CHECK(engine.sky().stars == 0.0f);
    const Vec3 noonFog = engine.environment().fogColor;

    engine.gameTime().setTime(0, 0, 0);
    CHECK(engine.runFrame());
    CHECK(engine.sky().stars > 0.5f);
    CHECK(engine.environment().sunDirection.y > 0.0f); // the moon lights the night
    CHECK(engine.environment().sunIntensity < 1.0f);
    CHECK(engine.environment().fogColor.r < noonFog.r);

    // Through the level change the clock runs on (it is not part of a world).
    engine.gameTime().setTime(3, 21, 15);
    const u64 before = engine.gameTime().totalMinutes();
    engine.camera().transform.position = Vec3(-30.0f, 1.7f, 20.0f); // into TRG_TO_CAVE
    CHECK(engine.runFrame());
    CHECK(engine.worldPath() == "testworld/cave.g7world");
    CHECK(engine.gameTime().day() == 3);
    CHECK(engine.gameTime().totalMinutes() - before <= 1);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("World vobs GPU: an invalid start time is an error")
{
    EngineConfig config = worldConfig();
    config.startTime = "25:00";
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message == "invalid start time '25:00' (expected HH:MM)");
}

// Start views in the engine (label "gpu"): --cam/--yaw/--pitch/--fly put the free camera there in fly mode
// while the player waits, --player moves the player; the view line reproduces the view.

#include "GlFixture.hpp"

#include <g7/physics/Character.hpp>
#include <g7/runtime/Engine.hpp>

#include <cmath>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig viewConfig()
{
    EngineConfig config;
    config.appName = "view";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER"; // feet at (34, 0, 0), looking west
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    g7::test::keepVideoAlive();
    return config;
}
} // namespace

TEST_CASE("Start view GPU: --cam/--yaw/--pitch/--fly place the free camera, the player waits")
{
    EngineConfig config = viewConfig();
    config.view.camera = Vec3(10.0f, 6.0f, -20.0f);
    config.view.yawDegrees = 45.0f;
    config.view.pitchDegrees = -10.0f;
    config.view.fly = true;
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.player() != nullptr);
    CHECK(engine.flyMode());
    CHECK_FALSE(engine.playerCameraActive());
    for (int i = 0; i < 10; ++i)
    {
        CHECK(engine.runFrame());
    }
    const Vec3 eye = engine.camera().transform.position;
    CHECK(glm::length(eye - Vec3(10.0f, 6.0f, -20.0f)) < 1e-3f); // no keys: it stays
    const Vec3 forward = engine.camera().transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    CHECK(glm::degrees(std::atan2(-forward.x, -forward.z)) == doctest::Approx(45.0f).epsilon(0.001));
    CHECK(glm::degrees(std::asin(forward.y)) == doctest::Approx(-10.0f).epsilon(0.001));
    CHECK(std::abs(engine.player()->feet().x - 34.0f) < 0.05f); // waits at its start point
    CHECK(
        engine.viewLine() ==
        "--world=testworld/camp.g7world --cam=10.00,6.00,-20.00 --yaw=45.0 --pitch=-10.0 --time=08:00 --fly");

    // F3 back to the player: the line describes the player now.
    engine.setFlyMode(false);
    CHECK(engine.playerCameraActive());
    CHECK(engine.viewLine().find("--player=34.00,") != std::string::npos);
    CHECK(engine.renderDevice()->debugErrorCount() == 0);
}

TEST_CASE("Start view GPU: --player and --yaw move and turn the player")
{
    EngineConfig config = viewConfig();
    config.view.player = Vec3(40.0f, 0.02f, 3.0f);
    config.view.yawDegrees = 180.0f; // facing +Z
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    REQUIRE(engine.player() != nullptr);
    CHECK_FALSE(engine.flyMode());
    for (int i = 0; i < 20; ++i)
    {
        CHECK(engine.runFrame());
    }
    CHECK(engine.player()->feet().x == doctest::Approx(40.0f).epsilon(0.001));
    CHECK(engine.player()->feet().z == doctest::Approx(3.0f).epsilon(0.001));
    CHECK(std::abs(std::remainder(glm::degrees(engine.playerMovement().yaw()) - 180.0f, 360.0f)) < 0.1f);
    CHECK(engine.viewLine().find("--player=40.00,0.00,3.00 --yaw=180.0 --time=08:00") != std::string::npos);
}

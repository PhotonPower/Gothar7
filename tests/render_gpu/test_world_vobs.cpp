// Vob types in the engine (label "gpu"): start points place the camera, triggers notice it, the debug
// overlay draws the invisible vobs.

#include "GlFixture.hpp"

#include <g7/runtime/Engine.hpp>
#include <g7/world/Components.hpp>

#include <cmath>
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
    CHECK(result.error().message ==
          "cannot load world: unknown start point 'START_NOWHERE' (the world has: START_LAGER)");
}

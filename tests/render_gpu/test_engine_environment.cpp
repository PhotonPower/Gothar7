// A world without sky (dungeon D7b, label "gpu"): testworld/cellar.g7world has an `environment` block - no
// sun, moon or stars, its own ambient light and fog, no weather to see - and a fog zone (a misty crypt) whose
// fog takes over at the camera.

#include "GlFixture.hpp"

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;

namespace
{
EngineConfig cellarConfig(const Vec3& camera)
{
    EngineConfig config;
    config.appName = "environment";
    config.window.size = {320, 180};
    config.window.vsync = false;
    config.world = fs::fromUtf8("testworld/cellar.g7world");
    config.start = "START_KELLER";
    config.startTime = "12:00";
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.view.camera = camera;
    config.view.fly = true;
    test::keepVideoAlive();
    return config;
}
} // namespace

TEST_CASE("Engine environment GPU: a cellar without sky at noon, its fog and the fog of a crypt zone")
{
    {
        Engine engine(cellarConfig(Vec3(-10.0f, 1.7f, -10.0f)));
        REQUIRE(engine.init().ok());
        REQUIRE(engine.runConsoleLine("set_weather('gewitter')").ok());
        for (int i = 0; i < 3; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        const render::Environment& env = engine.environment();
        CHECK(env.sunIntensity == 0.0f); // noon, but no sun
        CHECK(engine.sky().stars == 0.0f);
        CHECK(engine.sky().moon == 0.0f);
        CHECK(env.ambientSky.r < 0.02f); // sRGB 0.12: very dark, not black
        CHECK(env.ambientSky.r > 0.005f);
        CHECK(env.fogStart == doctest::Approx(2.0f));
        CHECK(render::fogFactor(30.0f, env.fogStart, env.fogDensity) == doctest::Approx(0.9f).epsilon(0.01));
        CHECK(env.wetness == 0.0f);
        // The weather runs on but does not show here.
        CHECK(engine.weather().current() == "gewitter");
        CHECK(engine.weatherNow().rain == 0.0f);
        CHECK(engine.weatherNow().lightning == 0.0f);
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
    }
    {
        // In the crypt (fog zone GRUFT): its fog, 90 % at 8 m.
        Engine engine(cellarConfig(Vec3(10.0f, 1.7f, 10.0f)));
        REQUIRE(engine.init().ok());
        for (int i = 0; i < 3; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        const render::Environment& env = engine.environment();
        CHECK(env.fogStart == doctest::Approx(0.5f));
        CHECK(render::fogFactor(8.0f, env.fogStart, env.fogDensity) == doctest::Approx(0.9f).epsilon(0.01));
        CHECK(env.fogColor.r > 0.1f); // sRGB 0.4 -> about 0.13 linear
    }
}

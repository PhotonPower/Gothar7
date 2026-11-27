// Dungeon level 1 (worlds/leonberg/dungeon/ebene_01.g7world, welt W8) without a shadow map - the sun stands
// below the ground: the lit passes still find a depth texture with a compare sampler at unit 3 (the shadow
// atlas' unit), so the GL debug output stays quiet (no "empty shadow sampler" warning every frame).

#include "GlFixture.hpp"

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace g7;

TEST_CASE("Engine render: dungeon level 1 without a shadow map keeps the GL debug output empty")
{
    for (const char* time : {"12:00", "00:00"})
    {
        CAPTURE(std::string(time));
        EngineConfig config;
        config.appName = "dungeon_render";
        config.window.size = {320, 180};
        config.window.vsync = false;
        config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
        config.world = fs::fromUtf8("worlds/leonberg/dungeon/ebene_01.g7world");
        config.start = "START_E01_VON_OBEN";
        config.startTime = time;
        config.fixedFrameSeconds = 1.0 / 60.0;
        test::keepVideoAlive();
        Engine engine(config);
        REQUIRE(engine.init().ok());
        for (int i = 0; i < 8; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        const render::Device* device = engine.renderDevice();
        REQUIRE(device != nullptr);
        CHECK(device->debugErrorCount() == 0);
        CHECK(device->debugUndefinedCount() == 0);
        CHECK(device->debugMessageCount(render::DebugSeverity::Low) == 0);
        CHECK(device->debugMessageCount(render::DebugSeverity::Medium) == 0);
        CHECK(device->debugMessageCount(render::DebugSeverity::High) == 0);
    }
}

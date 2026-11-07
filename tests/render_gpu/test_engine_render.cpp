#include "GlFixture.hpp"

#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>

TEST_CASE("Engine renders frames into an OpenGL window")
{
    g7::EngineConfig config;
    config.window.size = {320, 240};
    config.shaderDirectory = g7::fs::fromUtf8(G7_SHADER_DIR);
    config.maxFrames = 3;
    g7::test::keepVideoAlive();
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.renderDevice() != nullptr);
    CHECK(engine.run() == 0);
    CHECK(engine.frameCount() == 3);
}

TEST_CASE("Engine render: bloom and GPU pass times, bloom switched off in the settings (M17)")
{
    for (const bool bloom : {true, false})
    {
        auto settings = g7::Config::parse(bloom ? "[render]\nbloom = true\n" : "[render]\nbloom = false\n");
        REQUIRE(settings.ok());
        g7::EngineConfig config;
        config.window.size = {320, 240};
        config.window.vsync = false;
        config.shaderDirectory = g7::fs::fromUtf8(G7_SHADER_DIR);
        config.world = g7::fs::fromUtf8("testworld/camp.g7world");
        config.player = false;
        config.fixedFrameSeconds = 1.0 / 60.0;
        config.settings = std::move(settings).value();
        g7::test::keepVideoAlive();
        g7::Engine engine(config);
        REQUIRE(engine.init().ok());
        for (int i = 0; i < 12; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        CHECK(engine.bloomSettings().enabled == bloom);
        const auto has = [&](std::string_view name)
        {
            return std::ranges::any_of(engine.passTimes(), [&](const g7::render::PassTime& p)
                                       { return p.name == name && p.milliseconds > 0.0f; });
        };
        CHECK(has("scene"));
        CHECK(has("post"));
        CHECK(has("bloom") == bloom);
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
    }
}

TEST_CASE("Engine render: the pond in the camp is drawn by the water pass, by quality (M17 W1)")
{
    for (const std::string_view quality : {"low", "high"})
    {
        auto settings =
            g7::Config::parse(std::string("[render]\nwater_quality = \"") + std::string(quality) + "\"\n");
        REQUIRE(settings.ok());
        g7::EngineConfig config;
        config.window.size = {320, 240};
        config.window.vsync = false;
        config.shaderDirectory = g7::fs::fromUtf8(G7_SHADER_DIR);
        config.world = g7::fs::fromUtf8("testworld/camp.g7world");
        config.start = "START_TEICH"; // east shore of the pond, looking west over the water
        config.player = false;
        config.fixedFrameSeconds = 1.0 / 60.0;
        config.settings = std::move(settings).value();
        g7::test::keepVideoAlive();
        g7::Engine engine(config);
        REQUIRE(engine.init().ok());
        for (int i = 0; i < 12; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        REQUIRE(engine.waterSurfaces().size() == 1);
        // No kind in the camp: the default of data/water.toml (pond).
        CHECK(engine.waterSurfaces()[0].style == &engine.waterStyles().style("pond"));
        CHECK(engine.waterQuality() ==
              (quality == "low" ? g7::render::WaterQuality::Low : g7::render::WaterQuality::High));
        CHECK(engine.waterSurfacesDrawn() == 1);
        CHECK(std::ranges::any_of(engine.passTimes(), [](const g7::render::PassTime& p)
                                  { return p.name == "water" && p.milliseconds > 0.0f; }));
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
    }
}

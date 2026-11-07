#include "GlFixture.hpp"

#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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

TEST_CASE("Engine render: rain only while it rains and the weather is shown (M17 R)")
{
    const auto streaks = [](std::string_view weather, bool shown)
    {
        auto settings =
            g7::Config::parse(shown ? "[render]\nweather = true\n" : "[render]\nweather = false\n");
        REQUIRE(settings.ok());
        g7::EngineConfig config;
        config.window.size = {320, 240};
        config.window.vsync = false;
        config.shaderDirectory = g7::fs::fromUtf8(G7_SHADER_DIR);
        config.world = g7::fs::fromUtf8("testworld/camp.g7world");
        config.player = false;
        config.fixedFrameSeconds = 1.0 / 60.0;
        config.weather = std::string(weather);
        config.settings = std::move(settings).value();
        g7::test::keepVideoAlive();
        g7::Engine engine(config);
        REQUIRE(engine.init().ok());
        for (int i = 0; i < 6; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
        if (std::string_view(weather) == "regen" && shown)
        {
            CHECK(engine.rain().mapValid()); // the map from above was drawn
        }
        return engine.rainStreaksDrawn();
    };
    CHECK(streaks("regen", true) > 1000u);
    CHECK(streaks("regen", false) == 0u); // [render] weather = false: no rain to see
    CHECK(streaks("klar", true) == 0u);
}

#include "GlFixture.hpp"

#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <memory>
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

TEST_CASE(
    "Engine render: the camera under the pond sees the underwater view, music and ambience muffled (M17 W3)")
{
    for (const bool under : {true, false})
    {
        g7::EngineConfig config;
        config.window.size = {320, 240};
        config.window.vsync = false;
        config.shaderDirectory = g7::fs::fromUtf8(G7_SHADER_DIR);
        config.world = g7::fs::fromUtf8("testworld/camp.g7world");
        config.player = false;
        config.fixedFrameSeconds = 1.0 / 60.0;
        // The pond: surface -4, bottom -12, centre (-160, -137).
        config.view.camera = g7::Vec3(-160.0f, under ? -6.0f : -2.0f, -137.0f);
        g7::test::keepVideoAlive();
        g7::Engine engine(config);
        REQUIRE(engine.init().ok());
        for (int i = 0; i < 6; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        CHECK((engine.cameraWater() != nullptr) == under);
        CHECK(engine.postSettings().underwater.has_value() == under);
        if (under)
        {
            CHECK(engine.postSettings().bloom == nullptr); // no glow of the sun above through the water
        }
        if (const g7::audio::AudioSystem* audio = engine.audio())
        {
            CHECK((audio->busMuffle(g7::audio::Bus::Music) > 0.5f) == under);
            CHECK((audio->busMuffle(g7::audio::Bus::Ambient) > 0.5f) == under);
        }
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
    }
}

TEST_CASE("Engine render: ambient occlusion with depth prepass only when switched on (M17 P3)")
{
    const auto run = [](std::string_view settingsText)
    {
        auto settings = g7::Config::parse(settingsText);
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
        auto engine = std::make_unique<g7::Engine>(config);
        REQUIRE(engine->init().ok());
        for (int i = 0; i < 12; ++i)
        {
            REQUIRE(engine->runFrame());
        }
        return engine;
    };
    const auto has = [](const g7::Engine& engine, std::string_view name)
    {
        return std::ranges::any_of(engine.passTimes(), [&](const g7::render::PassTime& p)
                                   { return p.name == name && p.milliseconds > 0.0f; });
    };
    {
        auto off = run("[render]\nssao = false\n");
        CHECK_FALSE(off->ssaoEnabled());
        CHECK_FALSE(has(*off, "depth")); // no prepass without ambient occlusion
        CHECK_FALSE(has(*off, "ssao"));
        CHECK(off->renderDevice()->debugErrorCount() == 0);
    }
    auto on = run("[render]\nssao = true\n");
    CHECK(on->ssaoEnabled());
    CHECK(has(*on, "depth"));
    CHECK(has(*on, "ssao"));
    CHECK(on->ssao().result() != nullptr);
    CHECK(on->renderDevice()->debugErrorCount() == 0);
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

TEST_CASE(
    "Engine render: spell distortion only for distorting effects in sight, none when switched off (M17 V)")
{
    const auto drawn = [](bool on, std::string_view effect)
    {
        auto settings = g7::Config::parse(on ? "[render]\nspell_distortion = true\n"
                                             : "[render]\nspell_distortion = false\n");
        REQUIRE(settings.ok());
        g7::EngineConfig config;
        config.window.size = {320, 240};
        config.window.vsync = false;
        config.shaderDirectory = g7::fs::fromUtf8(G7_SHADER_DIR);
        config.world = g7::fs::fromUtf8("testworld/camp.g7world");
        config.start = "START_LAGER";
        config.fixedFrameSeconds = 1.0 / 60.0;
        config.settings = std::move(settings).value();
        g7::test::keepVideoAlive();
        g7::Engine engine(config);
        REQUIRE(engine.init().ok());
        REQUIRE(engine.runFrame());
        // In front of the hero (he looks west at the start).
        const g7::Vec3 feet = engine.player()->feet();
        if (!effect.empty())
        {
            REQUIRE(engine
                        .runConsoleLine(
                            std::format("fx('{}', {}, {}, {})", effect, feet.x - 3.0f, feet.y + 0.5f, feet.z))
                        .ok());
        }
        for (int i = 0; i < 10; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
        CHECK(engine.wobble() == 0.0f);
        return engine.distortionDrawn();
    };
    CHECK(drawn(true, "fire") > 0u);   // a camp fire flickers (decision 13)
    CHECK(drawn(false, "fire") == 0u); // switched off: no pass
    CHECK(drawn(true, "torch") == 0u); // a torch does not
    CHECK(drawn(true, "") == 0u);      // nothing distorting: no pass
}

TEST_CASE("Engine render: the wind follows the weather, stands still when switched off (M17 W)")
{
    const auto windOf = [](std::string_view weather, bool on)
    {
        auto settings = g7::Config::parse(on ? "[render]\nwind = true\n" : "[render]\nwind = false\n");
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
        REQUIRE(engine.runFrame());
        const g7::f32 start = engine.windNow().time;
        for (int i = 0; i < 5; ++i)
        {
            REQUIRE(engine.runFrame());
        }
        CHECK(engine.renderDevice()->debugErrorCount() == 0);
        const g7::render::WindFrame wind = engine.windNow();
        CHECK(wind.time > start); // the sway moves on
        CHECK(wind.direction == g7::world::windDirection(engine.gameTime().clock()));
        CHECK(engine.windEnabled() == on);
        return wind.strength;
    };
    CHECK(windOf("klar", true) == doctest::Approx(0.15f)); // always a light breeze (decision 5)
    CHECK(windOf("gewitter", true) == doctest::Approx(0.8f));
    CHECK(windOf("gewitter", false) == 0.0f); // [render] wind = false: everything stands still
}

TEST_CASE("Engine render: no rain streaks under water (M17 R)")
{
    g7::EngineConfig config;
    config.window.size = {320, 240};
    config.window.vsync = false;
    config.shaderDirectory = g7::fs::fromUtf8(G7_SHADER_DIR);
    config.world = g7::fs::fromUtf8("testworld/camp.g7world");
    config.player = false;
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.weather = "regen";
    config.view.camera = g7::Vec3(-160.0f, -6.0f, -137.0f); // in the pond (surface -4)
    g7::test::keepVideoAlive();
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    for (int i = 0; i < 6; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    REQUIRE(engine.cameraWater() != nullptr);
    CHECK(engine.rainStreaksDrawn() == 0u);
    if (const g7::audio::AudioSystem* audio = engine.audio())
    {
        CHECK(engine.rainSound() == "amb_rain");
        CHECK(audio->busMuffle(g7::audio::Bus::Ambient) > 0.5f); // the rain heard dull through the water (W3)
    }
}

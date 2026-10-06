// Daylight through windows (label "headless", contract with welt in world.md): a light with
// components.light.daylight follows the sky - its ambient's colour, its intensity times the sky's brightness
// relative to noon (night: about 0); other lights stay as they are.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace g7;

TEST_CASE("Engine lighting: a daylight window light follows the sky; at night it is out")
{
    const std::filesystem::path world = std::filesystem::temp_directory_path() / "g7_daylight.g7world";
    {
        std::ofstream out(world, std::ios::binary);
        out << R"({"version":1,"name":"window","nextVobId":4,"vobs":[)"
            << R"({"id":1,"type":"start","name":"START","pos":[0.0,0.0,0.0],"rot":[0.0,0.0,0.0,1.0]},)"
            << R"({"id":2,"type":"light","name":"WINDOW","pos":[1.0,1.5,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("components":{"light":{"color":[1.0,1.0,1.0],"range":4.0,"intensity":2.0,"daylight":true}}},)"
            << R"({"id":3,"type":"light","name":"LAMP","pos":[-1.0,1.5,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("components":{"light":{"color":[1.0,0.6,0.3],"range":6.0,"intensity":3.0}}}]})";
    }
    const auto at = [&](const char* time)
    {
        EngineConfig config;
        config.appName = "daylight";
        config.headless = true;
        config.world = world;
        config.start = "START";
        config.startTime = time;
        Engine engine(std::move(config));
        REQUIRE(engine.init().ok());
        const auto lights = engine.daylightLights();
        REQUIRE(lights.size() == 1); // the lamp is no daylight light
        return lights[0];
    };
    const render::PointLight noon = at("12:00");
    CHECK(noon.intensity == doctest::Approx(2.0f).epsilon(0.02));
    CHECK(std::max({noon.color.r, noon.color.g, noon.color.b}) == doctest::Approx(1.0f));
    CHECK(noon.radius == doctest::Approx(4.0f));
    const render::PointLight evening = at("19:30");
    CHECK(evening.intensity < noon.intensity);
    const render::PointLight night = at("01:00");
    CHECK(night.intensity < 0.25f * noon.intensity);
    std::filesystem::remove(world);
}

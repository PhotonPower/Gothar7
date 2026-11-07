// Colour grading by zone (M17 P2): in the camp the time of day alone, in the forest around it [zone.wald]
// (decision 3: slightly green); the grading follows the time of day.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

using namespace g7;

TEST_CASE("Engine grading: zones override the time of day")
{
    EngineConfig config;
    config.appName = "grading";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    Engine engine(config);
    REQUIRE(engine.init().ok());
    render::Grading noon;
    noon.temperature = 0.0f;
    // The camp (ambient "camp", music "LAGER"): no zone grading - the time of day stays.
    const render::Grading camp = engine.gradingAt(noon, Vec3(8.0f, 1.0f, 4.0f));
    CHECK(camp == noon);
    // The forest around it (ambient "wald"): green tint.
    const render::Grading forest = engine.gradingAt(noon, Vec3(150.0f, 1.0f, 150.0f));
    CHECK(forest.tint < 0.0f);
    CHECK(forest.temperature == doctest::Approx(noon.temperature));
}

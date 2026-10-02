#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

TEST_CASE("Engine runs a bounded headless loop")
{
    g7::EngineConfig config;
    config.maxFrames = 3;
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    CHECK(engine.run() == 0);
    CHECK(engine.frameCount() == 3);
}

TEST_CASE("Engine rejects invalid config")
{
    g7::EngineConfig config;
    config.simulationHz = 0.0;
    g7::Engine engine(config);
    CHECK_FALSE(engine.init().ok());
}

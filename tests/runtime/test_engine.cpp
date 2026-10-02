#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

TEST_CASE("Engine runs a bounded headless loop")
{
    g7::EngineConfig config;
    config.headless = true;
    config.maxFrames = 3;
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    CHECK(engine.window() == nullptr);
    CHECK(engine.run() == 0);
    CHECK(engine.frameCount() == 3);
}

TEST_CASE("Engine rejects invalid config")
{
    g7::EngineConfig config;
    config.headless = true;
    config.simulationHz = 0.0;
    g7::Engine engine(config);
    CHECK_FALSE(engine.init().ok());
}

// The following tests need SDL_VIDEO_DRIVER=offscreen (set by CTest) on machines without display.
TEST_CASE("Engine runs a bounded loop with a window")
{
    g7::EngineConfig config;
    config.maxFrames = 3;
    config.window.size = {320, 240};
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.window() != nullptr);
    CHECK(engine.window()->size() == g7::platform::Extent{320, 240});
    CHECK(engine.run() == 0);
    CHECK(engine.frameCount() == 3);
}

TEST_CASE("Engine stops when the window is closed")
{
    g7::EngineConfig config;
    config.window.size = {320, 240};
    g7::Engine engine(config); // no frame limit: only the close request ends the loop
    REQUIRE(engine.init().ok());
    engine.window()->requestClose();
    CHECK(engine.run() == 0);
    CHECK(engine.frameCount() == 1);
}

TEST_CASE("Engine reports an invalid window description")
{
    g7::EngineConfig config;
    config.window.size = {0, 0};
    g7::Engine engine(config);
    auto result = engine.init();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("window") != std::string::npos);
}

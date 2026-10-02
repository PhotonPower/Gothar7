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
    config.render = false; // offscreen driver has no OpenGL; GL is covered by the render_gpu suite
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
    config.render = false;     // offscreen driver has no OpenGL; GL is covered by the render_gpu suite
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
    config.render = false;
    g7::Engine engine(config);
    auto result = engine.init();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("window") != std::string::npos);
}

TEST_CASE("Engine builds the action map of the configured scheme")
{
    auto settings = g7::Config::parse(R"(
[input]
scheme = "modern"
[bindings.modern]
jump = ["Space"]
[bindings.classic]
jump = ["LeftAlt"]
)");
    REQUIRE(settings.ok());

    g7::EngineConfig config;
    config.maxFrames = 1;
    config.window.size = {320, 240};
    config.render = false; // offscreen driver has no OpenGL; GL is covered by the render_gpu suite
    config.settings = std::move(settings).value();
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    const auto jump = engine.actions().bindings(g7::platform::Action::Jump);
    REQUIRE(jump.size() == 1);
    CHECK(jump[0] == g7::platform::InputBinding(g7::platform::Key::Space));
}

namespace
{
g7::EngineConfig deterministicHeadless()
{
    g7::EngineConfig config;
    config.headless = true;
    config.fixedFrameSeconds = 1.0 / 60.0; // one simulation step per frame at 60 Hz
    return config;
}

void runFrames(g7::Engine& engine, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        REQUIRE(engine.runFrame());
    }
}
} // namespace

TEST_CASE("Engine: fixed frame time gives deterministic ticks")
{
    g7::Engine engine(deterministicHeadless());
    REQUIRE(engine.init().ok());
    runFrames(engine, 60);
    CHECK(engine.frameCount() == 60);
    CHECK(engine.simulationTicks() == 60);
    CHECK(engine.simulationTime() == doctest::Approx(1.0));
}

TEST_CASE("Engine: time scale slows down and speeds up the simulation")
{
    g7::Engine engine(deterministicHeadless());
    REQUIRE(engine.init().ok());
    engine.setTimeScale(0.5);
    runFrames(engine, 60);
    CHECK(engine.simulationTicks() == 30);

    engine.setTimeScale(2.0);
    runFrames(engine, 10);
    CHECK(engine.simulationTicks() == 50);

    engine.setTimeScale(-1.0);
    CHECK(engine.timeScale() == 0.0);
    runFrames(engine, 10);
    CHECK(engine.simulationTicks() == 50);
    engine.setTimeScale(1000.0);
    CHECK(engine.timeScale() == g7::Engine::kMaxTimeScale);
}

TEST_CASE("Engine: pause stops the simulation without catching up")
{
    g7::Engine engine(deterministicHeadless());
    REQUIRE(engine.init().ok());
    runFrames(engine, 10);
    REQUIRE(engine.simulationTicks() == 10);

    engine.setPaused(true);
    CHECK(engine.paused());
    runFrames(engine, 30);
    CHECK(engine.frameCount() == 40); // frames keep running
    CHECK(engine.simulationTicks() == 10);

    engine.setPaused(false);
    runFrames(engine, 1);
    CHECK(engine.simulationTicks() == 11); // exactly one step, no backlog
}

TEST_CASE("Engine: runFrame reports quit")
{
    g7::EngineConfig config = deterministicHeadless();
    config.maxFrames = 2;
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    CHECK(engine.runFrame());
    CHECK_FALSE(engine.runFrame()); // maxFrames reached
    CHECK_FALSE(engine.runFrame()); // stays stopped
    CHECK(engine.frameCount() == 2);
}

TEST_CASE("Engine: frame cap with a window")
{
    g7::EngineConfig config;
    config.window.size = {320, 240};
    config.render = false; // offscreen driver has no OpenGL; GL is covered by the render_gpu suite
    config.maxFps = 100.0;
    config.maxFrames = 11;
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    const g7::Stopwatch watch;
    CHECK(engine.run() == 0);
    // 10 capped intervals of 10 ms (the last frame does not wait). Only the lower bound is
    // checked: CI machines may be slower, never faster.
    CHECK(watch.elapsedSeconds() >= 0.09);
}

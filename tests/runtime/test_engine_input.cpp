// Engine reactions to input, driven by synthetic SDL events (needs SDL_VIDEO_DRIVER=offscreen).

#include <g7/runtime/Engine.hpp>

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

namespace
{
void pushKey(SDL_Scancode scancode, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    REQUIRE(SDL_PushEvent(&event));
}
} // namespace

TEST_CASE("Engine: the pause action opens the pause menu, which stops the simulation (M14 part F)")
{
    auto settings = g7::Config::parse(R"(
[bindings.classic]
pause = ["Escape"]
)");
    REQUIRE(settings.ok());

    g7::EngineConfig config;
    config.window.size = {320, 240};
    config.render = false; // offscreen driver has no OpenGL; GL is covered by the render_gpu suite
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.settings = std::move(settings).value();
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame()); // drain startup events
    const auto ticksBefore = engine.simulationTicks();

    pushKey(SDL_SCANCODE_ESCAPE, true);
    REQUIRE(engine.runFrame());
    REQUIRE(engine.gameUi() != nullptr);
    CHECK(engine.screens().find("menu") != nullptr);
    CHECK(engine.screens().pausesGame());
    pushKey(SDL_SCANCODE_ESCAPE, false);
    REQUIRE(engine.runFrame());
    REQUIRE(engine.runFrame());
    CHECK(engine.simulationTicks() == ticksBefore); // the pressing frame already skipped its step

    pushKey(SDL_SCANCODE_ESCAPE, true); // the pause action closes the menu again (Start on the gamepad)
    REQUIRE(engine.runFrame());
    pushKey(SDL_SCANCODE_ESCAPE, false);
    REQUIRE(engine.runFrame());
    CHECK(engine.screens().empty());
    REQUIRE(engine.runFrame());
    CHECK(engine.simulationTicks() > ticksBefore);
}

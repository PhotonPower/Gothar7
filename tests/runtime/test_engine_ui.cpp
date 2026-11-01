// The game UI in the engine (M14 part B), driven by synthetic SDL events (needs SDL_VIDEO_DRIVER=offscreen):
// the sample screen takes the keys, Escape closes it before it can pause the game.

#include <g7/runtime/Engine.hpp>
#include <g7/ui/SamplePage.hpp>

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

namespace
{
void tapKey(g7::Engine& engine, SDL_Scancode scancode)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    event.key.down = true;
    REQUIRE(SDL_PushEvent(&event));
    REQUIRE(engine.runFrame());
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    REQUIRE(SDL_PushEvent(&event));
    REQUIRE(engine.runFrame());
}
} // namespace

TEST_CASE("Engine UI: the sample screen takes the keys, Escape closes it first")
{
    auto settings = g7::Config::parse(R"(
[ui]
language = "de"
[bindings.classic]
pause = ["Escape"]
ui_down = ["Down"]
ui_up = ["Up"]
ui_accept = ["Enter"]
ui_back = ["Escape"]
)");
    REQUIRE(settings.ok());
    g7::EngineConfig config;
    config.window.size = {640, 360};
    config.render = false; // offscreen driver has no OpenGL; drawing is covered by the render_gpu suite
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.uiSample = true;
    config.settings = std::move(settings).value();
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.gameUi() != nullptr);
    REQUIRE(engine.runFrame()); // drain startup events
    auto* sample = dynamic_cast<g7::ui::SampleScreen*>(engine.screens().find("sample"));
    REQUIRE(sample != nullptr);
    CHECK(engine.screens().blocksGameInput());
    CHECK(sample->focus() == &sample->list());

    tapKey(engine, SDL_SCANCODE_DOWN);
    tapKey(engine, SDL_SCANCODE_DOWN);
    CHECK(sample->list().selected() == 2);
    tapKey(engine, SDL_SCANCODE_RETURN);
    CHECK(sample->chosen().value == "UI_SAMPLE_CHOSEN_ROW");
    CHECK(sample->chosen().resolve(engine.gameUi()->strings()) == "Gewählt: Eintrag 3");

    tapKey(engine, SDL_SCANCODE_ESCAPE); // ui_back: the screen closes, the game does not pause
    CHECK(engine.screens().empty());
    CHECK_FALSE(engine.paused());
    tapKey(engine, SDL_SCANCODE_ESCAPE); // now the game has the key
    CHECK(engine.paused());
}

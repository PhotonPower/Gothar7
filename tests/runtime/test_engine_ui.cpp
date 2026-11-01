// The game UI in the engine (M14 part B), driven by synthetic SDL events (needs SDL_VIDEO_DRIVER=offscreen):
// the sample screen takes the keys, Escape closes it before it can pause the game.

#include <g7/runtime/Engine.hpp>
#include <g7/ui/GameScreens.hpp>
#include <g7/ui/ItemScreen.hpp>
#include <g7/ui/SamplePage.hpp>

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <functional>

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

TEST_CASE("Engine UI: screen messages from Lua")
{
    g7::EngineConfig config;
    config.window.size = {640, 360};
    config.render = false;
    config.fixedFrameSeconds = 1.0 / 60.0;
    g7::Engine engine(config);
    REQUIRE(engine.init().ok());
    REQUIRE(engine.gameUi() != nullptr);
    REQUIRE(engine.runConsoleLine("screen_message(\"Erfahrung +50\")").ok());
    REQUIRE(engine.runConsoleLine("screen_message(\"Kurz\", 0.5)").ok());
    CHECK_FALSE(engine.runConsoleLine("screen_message(42)").ok());
    REQUIRE(engine.screenMessages().messages().size() == 2);
    CHECK(engine.screenMessages().messages().front().text == "Erfahrung +50");
    for (int i = 0; i < 60; ++i) // one second: the short one is gone (F7: 3 s by default)
    {
        REQUIRE(engine.runFrame());
    }
    REQUIRE(engine.screenMessages().messages().size() == 1);
    CHECK(engine.screenMessages().messages().front().text == "Erfahrung +50");
}

namespace
{
/// The test camp with the shipped bindings, a window (offscreen driver) and no renderer.
g7::EngineConfig campWithWindow()
{
    auto settings = g7::Config::load(g7::fs::fromUtf8(G7_GAME_CONFIG));
    REQUIRE(settings.ok());
    g7::EngineConfig config;
    config.window.size = {640, 360};
    config.render = false;
    config.world = g7::fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.settings = std::move(settings).value();
    return config;
}

bool runUntil(g7::Engine& engine, const std::function<bool()>& done, int limit = 600)
{
    for (int i = 0; i < limit && !done(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    return done();
}
} // namespace

TEST_CASE("Engine UI: lockpicking, documents and the dialogue open as game screens")
{
    g7::Engine engine(campWithWindow());
    REQUIRE(engine.init().ok());
    REQUIRE(engine.gameUi() != nullptr);
    REQUIRE(engine.runFrame());
    CHECK(engine.hud().model().visible);                // the hero's health bar
    CHECK_FALSE(engine.hud().model().mana.has_value()); // no magic yet (F3)

    // Lockpicking: the screen turns with left/right (combination LRRLR), back stops.
    REQUIRE(engine.runConsoleLine("give_item('it_lockpick', 2)").ok());
    engine.setRandomSource([] { return 0.9f; }); // a wrong turn would not break the pick
    const auto locked = engine.findMob("LAGER_TRUHE_ZU");
    REQUIRE(locked.has_value());
    REQUIRE(engine.useMob(*locked).ok());
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("lockpick") != nullptr; }));
    auto* lockpick = dynamic_cast<g7::ui::LockpickScreen*>(engine.screens().find("lockpick"));
    REQUIRE(lockpick != nullptr);
    CHECK(engine.screens().blocksGameInput());
    tapKey(engine, SDL_SCANCODE_LEFT);
    tapKey(engine, SDL_SCANCODE_RIGHT);
    CHECK(lockpick->view().progress == 2);
    tapKey(engine, SDL_SCANCODE_ESCAPE);
    REQUIRE(runUntil(engine, [&] { return !engine.mobPhase(); }));
    REQUIRE(engine.runFrame());
    CHECK(engine.screens().find("lockpick") == nullptr);
    CHECK_FALSE(engine.paused()); // Esc only stopped the lockpicking

    // A letter: reading opens the document screen, the action key closes it.
    REQUIRE(engine.runConsoleLine("give_item('it_letter_farm')").ok());
    REQUIRE(engine.runConsoleLine("use_item('it_letter_farm')").ok());
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("document") != nullptr; }));
    tapKey(engine, SDL_SCANCODE_RETURN);
    CHECK_FALSE(engine.document().has_value());
    CHECK(engine.screens().find("document") == nullptr);

    // A dialogue: its screen hides the HUD; it closes when the dialogue ends.
    REQUIRE(engine.runConsoleLine("insert_npc('npc_gate_guard')").ok());
    REQUIRE(engine.runConsoleLine("talk('npc_gate_guard')").ok());
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("dialog") != nullptr; }));
    REQUIRE(engine.runFrame());
    CHECK_FALSE(engine.hud().model().visible);
    const auto inMenu = [&]
    {
        auto menu = engine.runConsoleLine("dialog_state() ~= nil and (dialog_state().line or '') == ''");
        return menu.ok() && menu.value().asBool();
    };
    REQUIRE(runUntil(engine, inMenu, 2400));
    auto* dialog = dynamic_cast<g7::ui::DialogScreen*>(engine.screens().find("dialog"));
    REQUIRE(dialog != nullptr);
    REQUIRE(runUntil(engine, [&] { return !dialog->options().rows.empty(); }, 120));
    CHECK(dialog->focus() == &dialog->options());
}

TEST_CASE("Engine UI: inventory and chest as item screens")
{
    g7::Engine engine(campWithWindow());
    REQUIRE(engine.init().ok());
    REQUIRE(engine.gameUi() != nullptr);
    REQUIRE(engine.runFrame());

    // The inventory: opened like the Tab key, Tab closes it again (the screen holds the other game keys).
    REQUIRE(engine.runConsoleLine("give_item('it_bread', 2)").ok());
    REQUIRE(engine.runConsoleLine("inventory_open()").ok());
    REQUIRE(engine.runFrame());
    auto* inventory = dynamic_cast<g7::ui::ItemScreen*>(engine.screens().find("inventory"));
    REQUIRE(inventory != nullptr);
    CHECK(engine.screens().blocksGameInput());
    CHECK_FALSE(engine.screens().pausesGame()); // F4
    tapKey(engine, SDL_SCANCODE_TAB);
    CHECK_FALSE(engine.inventoryOpen());
    CHECK(engine.screens().find("inventory") == nullptr);

    // A chest: the container screen; Enter on its stack asks how many (all by default), Enter takes them.
    const auto chest = engine.findMob("LAGER_TRUHE");
    REQUIRE(chest.has_value());
    REQUIRE(engine.runConsoleLine("use_mob('LAGER_TRUHE')").ok());
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("container") != nullptr; }));
    REQUIRE(engine.runFrame());
    auto* container = dynamic_cast<g7::ui::ItemScreen*>(engine.screens().find("container"));
    REQUIRE(container != nullptr);
    REQUIRE_FALSE(container->otherItems().empty());
    CHECK(container->focus() == &container->otherGrid());
    const g7::ui::ItemEntry first = *container->selected();
    const g7::u32 before = engine.hero()->itemCount(first.item);
    tapKey(engine, SDL_SCANCODE_RETURN);
    if (first.count > 1)
    {
        REQUIRE(container->quantityOpen());
        CHECK(container->quantity() == first.count);
        tapKey(engine, SDL_SCANCODE_RETURN);
    }
    CHECK(engine.hero()->itemCount(first.item) == before + first.count);
    tapKey(engine, SDL_SCANCODE_ESCAPE); // leaves the chest
    REQUIRE(runUntil(engine, [&] { return !engine.mobPhase(); }));
    REQUIRE(engine.runFrame());
    CHECK(engine.screens().find("container") == nullptr);
}

TEST_CASE("Engine UI: character sheet, diary and map with their keys")
{
    g7::Engine engine(campWithWindow());
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame());
    tapKey(engine, SDL_SCANCODE_B); // status
    CHECK(engine.statusOpen());
    REQUIRE(engine.screens().find("status") != nullptr);
    tapKey(engine, SDL_SCANCODE_B); // closes it again
    CHECK_FALSE(engine.statusOpen());
    REQUIRE(engine.runFrame());
    CHECK(engine.screens().find("status") == nullptr);

    tapKey(engine, SDL_SCANCODE_N); // log
    CHECK(engine.diaryOpen());
    CHECK(engine.screens().find("diary") != nullptr);
    tapKey(engine, SDL_SCANCODE_ESCAPE);
    CHECK_FALSE(engine.diaryOpen());
    CHECK_FALSE(engine.paused());

    // Without a map item the map key only says so (F9); a map picture opens the map screen.
    tapKey(engine, SDL_SCANCODE_M);
    CHECK_FALSE(engine.mapOpen());
    CHECK(engine.screenMessages().messages().back().text == "Ich habe keine Karte.");
    CHECK_FALSE(engine.runConsoleLine("map_open('ui/maps/none.png')").ok());
    REQUIRE(engine.runConsoleLine("map_open('ui/maps/placeholder.png', 'Karte')").ok());
    REQUIRE(engine.runFrame());
    CHECK(engine.screens().find("map") != nullptr);
    tapKey(engine, SDL_SCANCODE_M);
    CHECK_FALSE(engine.mapOpen());
}

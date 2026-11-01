// Save slots in the game (M15 part D) with the shipped keys (synthetic SDL events, offscreen driver): F5
// quick save and F9 quick load, saving from the pause menu into a slot, the autosave after a level change,
// loading from the main menu of a new run; "Load" is greyed out while there is no save.

#include <g7/core/FileSystem.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/save/SaveFile.hpp>
#include <g7/ui/MenuScreens.hpp>
#include <g7/ui/SaveScreens.hpp>

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <filesystem>
#include <functional>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
void tap(Engine& engine, SDL_Scancode scancode)
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

bool runUntil(Engine& engine, const std::function<bool()>& done, int limit = 900)
{
    for (int i = 0; i < limit && !done(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    return done();
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    REQUIRE_MESSAGE(result.ok(), std::string(line), ": ", (result.ok() ? "" : result.error().message));
    return result.value();
}

EngineConfig menuConfig(const std::filesystem::path& saves)
{
    auto settings = Config::load(fs::fromUtf8(G7_GAME_CONFIG));
    REQUIRE(settings.ok());
    Config merged = settings.value();
    merged.set<std::string>("menu.world", "testworld/camp.g7world");
    merged.set<std::string>("menu.flight", "");
    merged.set<std::string>("ui.language", "de");
    EngineConfig config;
    config.window.size = {640, 360};
    config.render = false;
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.mainMenu = true;
    config.startTime = "12:00";
    config.defaultSettings = settings.value();
    config.settings = merged;
    config.saveDirectory = saves;
    return config;
}

bool inGame(Engine& engine)
{
    return !engine.loading() && engine.player() != nullptr && !engine.inMainMenu();
}
} // namespace

TEST_CASE("Save slots: quick save and load, a slot from the pause menu, the autosave, loading from the menu")
{
    const std::filesystem::path saves = std::filesystem::temp_directory_path() / "g7_save_slots_engine";
    std::filesystem::remove_all(saves);
    {
        Engine engine(menuConfig(saves));
        REQUIRE(engine.init().ok());
        REQUIRE(runUntil(engine, [&] { return engine.screens().find("menu") != nullptr; }));
        auto* menu = dynamic_cast<ui::MenuScreen*>(engine.screens().find("menu"));
        REQUIRE(menu != nullptr);
        CHECK(menu->entries().isDimmed(1)); // "Spiel laden": nothing saved yet
        tap(engine, SDL_SCANCODE_RETURN);   // new game
        REQUIRE(runUntil(engine, [&] { return inGame(engine); }));
        REQUIRE(runUntil(engine, [&] { return engine.saveBlocker().empty(); }, 120)); // on the ground

        // Not in a dialogue (S1).
        run(engine, "insert_npc('npc_old_man', 'wp_camp_center')");
        REQUIRE(run(engine, "talk('npc_old_man')").asBool());
        CHECK(engine.saveBlocker() == "UI_SAVE_NOT_NOW");
        tap(engine, SDL_SCANCODE_F5);
        CHECK(engine.lastNotice() == "Jetzt kann nicht gespeichert werden.");
        CHECK_FALSE(std::filesystem::exists(saves / "quick.g7save"));
        run(engine, "end_dialog()");
        REQUIRE(runUntil(engine, [&] { return !engine.inDialog(); }, 120));
        REQUIRE(runUntil(engine, [&] { return engine.screens().empty(); }, 30));

        // F5: the quick save; F9 loads it back.
        run(engine, "Story.slot_test = 1");
        tap(engine, SDL_SCANCODE_F5);
        CHECK(engine.lastNotice() == "Schnell gespeichert.");
        REQUIRE(std::filesystem::exists(saves / "quick.g7save"));
        auto quick = save::readSaveMeta(saves / "quick.g7save");
        REQUIRE(quick.ok());
        CHECK(quick.value().name.starts_with("Schnellspeicher: "));
        CHECK(quick.value().world == "testworld/camp.g7world");
        run(engine, "Story.slot_test = 2");
        tap(engine, SDL_SCANCODE_F9);
        REQUIRE(runUntil(engine, [&] { return inGame(engine) && engine.screens().empty(); }));
        CHECK(run(engine, "Story.slot_test").asInteger() == 1);

        // The pause menu: "Spiel speichern" (4th entry) -> the slots -> slot_01 (empty: no question).
        tap(engine, SDL_SCANCODE_ESCAPE);
        menu = dynamic_cast<ui::MenuScreen*>(engine.screens().find("menu"));
        REQUIRE(menu != nullptr);
        CHECK_FALSE(menu->entries().isDimmed(2)); // load: the quick save is there
        CHECK_FALSE(menu->entries().isDimmed(3)); // save
        for (int i = 0; i < 3; ++i)
        {
            tap(engine, SDL_SCANCODE_DOWN);
        }
        tap(engine, SDL_SCANCODE_RETURN);
        auto* slots = dynamic_cast<ui::SaveScreen*>(engine.screens().find("save"));
        REQUIRE(slots != nullptr);
        REQUIRE(engine.runFrame());
        CHECK(slots->view().rows.size() == save::kSaveSlots + 1); // the autosave is not offered
        tap(engine, SDL_SCANCODE_RETURN);
        REQUIRE(engine.runFrame());
        CHECK(engine.screens().empty()); // saved, back in the game
        REQUIRE(std::filesystem::exists(saves / "slot_01.g7save"));
        auto slot1 = save::readSaveMeta(saves / "slot_01.g7save");
        REQUIRE(slot1.ok());
        CHECK(slot1.value().name.find("Kapitel 1") != std::string::npos);
        CHECK(slot1.value().day == 1);

        // A level change saves automatically (S3).
        run(engine, "Story.slot_test = 3");
        engine.requestWorldChange("testworld/cave.g7world", "START_HOEHLE");
        REQUIRE(runUntil(engine, [&] { return std::filesystem::exists(saves / "auto.g7save"); }, 30));
    }

    // A new run: "Spiel laden" in the main menu, slot_01 (the first row) - the game as it was saved there.
    Engine engine(menuConfig(saves));
    REQUIRE(engine.init().ok());
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("menu") != nullptr; }));
    auto* menu = dynamic_cast<ui::MenuScreen*>(engine.screens().find("menu"));
    REQUIRE(menu != nullptr);
    CHECK_FALSE(menu->entries().isDimmed(1));
    tap(engine, SDL_SCANCODE_DOWN);
    tap(engine, SDL_SCANCODE_RETURN);
    auto* slots = dynamic_cast<ui::SaveScreen*>(engine.screens().find("load"));
    REQUIRE(slots != nullptr);
    REQUIRE(engine.runFrame());
    CHECK(slots->view().rows.size() == save::kSaveSlots + 2);
    CHECK_FALSE(slots->slots().isDimmed(0)); // slot_01
    CHECK(slots->slots().isDimmed(1));       // slot_02: empty
    tap(engine, SDL_SCANCODE_RETURN);
    REQUIRE(runUntil(engine, [&] { return inGame(engine) && engine.screens().empty(); }));
    CHECK(engine.worldPath() == "testworld/camp.g7world");
    CHECK(run(engine, "Story.slot_test").asInteger() == 1);
    CHECK(engine.playSeconds() > 0.0);
    std::filesystem::remove_all(saves);
}

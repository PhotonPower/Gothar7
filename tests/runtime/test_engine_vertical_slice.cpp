// M14 DoD (docs/03-roadmap.md): "Das Vertical Slice ist vollständig ohne Debug-UI spielbar". The test camp
// played through the game UI with real key presses (synthetic SDL events, shipped bindings, offscreen video
// driver): main menu -> new game -> dialogue -> quest -> trading -> inventory (equip, eat) -> chest -> fight
// -> diary -> pause menu; and in English after switching the language in the options. The console only sets
// the scene (who stands where) and reads state; the ImGui game windows are gone (ImGui is debug and editor
// UI, ADR 0009). Its own ctest group (runtime_slice).

#include <g7/core/FileSystem.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/GameScreens.hpp>
#include <g7/ui/ItemScreen.hpp>
#include <g7/ui/JournalScreens.hpp>
#include <g7/ui/MenuScreens.hpp>

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
void key(Engine& /*engine*/, SDL_Scancode scancode, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    REQUIRE(SDL_PushEvent(&event));
}

void frames(Engine& engine, int n = 1)
{
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(engine.runFrame());
        // ImGui only for debugging (ADR 0009): no window of it opens while playing.
        REQUIRE_FALSE(engine.debugUiFrame());
    }
}

void tap(Engine& engine, SDL_Scancode scancode)
{
    key(engine, scancode, true);
    frames(engine);
    key(engine, scancode, false);
    frames(engine);
}

bool runUntil(Engine& engine, const std::function<bool()>& done, int limit = 600)
{
    for (int i = 0; i < limit && !done(); ++i)
    {
        frames(engine);
    }
    return done();
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    REQUIRE_MESSAGE(result.ok(), std::string(line), ": ", (result.ok() ? "" : result.error().message));
    return result.value();
}

/// Sets the hero next to `at` (1.4 m to the side) and turns him with the turn key until `name` is in focus.
void approach(Engine& engine, const Vec3& at, std::string_view name)
{
    run(engine, std::format("teleport({}, {}, {})", at.x + 1.4f, at.y + 0.1f, at.z));
    frames(engine, 5);
    const auto focused = [&]
    {
        const auto focus = engine.focus();
        return focus && focus->name == name;
    };
    key(engine, SDL_SCANCODE_LEFT, true); // turn_left (classic)
    const bool found = runUntil(engine, focused, 300);
    key(engine, SDL_SCANCODE_LEFT, false);
    frames(engine);
    REQUIRE_MESSAGE(found, "not in focus: ", std::string(name));
}

/// Puts an NPC of the camp where its daily routine has it now (12:00), standing still there.
Vec3 placeNpc(Engine& engine, std::string_view npc)
{
    REQUIRE(run(engine, std::format("insert_npc('{}')", npc)).isString());
    run(engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    frames(engine, 3);
    const auto id = engine.npcByInstance(npc);
    REQUIRE(id.has_value());
    const auto position = engine.creaturePosition(*id);
    REQUIRE(position.has_value());
    return *position;
}

std::vector<std::string> dialogOptions(Engine& engine)
{
    std::vector<std::string> out;
    const script::Value options = run(engine, "dialog_state().options");
    if (const script::Table* t = options.asTable())
    {
        for (const script::Value& v : t->array)
        {
            out.emplace_back(v.asString());
        }
    }
    return out;
}

bool dialogMenuShown(Engine& engine)
{
    auto* dialog = dynamic_cast<ui::DialogScreen*>(engine.screens().find("dialog"));
    return engine.inDialog() && dialog != nullptr && !engine.trading() &&
           run(engine, "dialog_state().line or ''").asString().empty() && !dialog->options().rows.empty();
}

/// Skips the lines with the accept key (Enter) until the menu shows, then picks `text` with Down and Enter.
void choose(Engine& engine, std::string_view text)
{
    for (int i = 0; i < 400 && !dialogMenuShown(engine); ++i)
    {
        if (engine.inDialog() && !run(engine, "dialog_state().line or ''").asString().empty())
        {
            tap(engine, SDL_SCANCODE_RETURN); // skips the line said
        }
        else
        {
            frames(engine);
        }
    }
    REQUIRE_MESSAGE(dialogMenuShown(engine), "no dialogue menu for: ", std::string(text));
    frames(engine, 2); // the screen shows this menu
    // What the player reads: the rows of the dialogue screen (in the UI language).
    auto* screen = dynamic_cast<ui::DialogScreen*>(engine.screens().find("dialog"));
    REQUIRE(screen != nullptr);
    REQUIRE(screen->options().rows.size() == dialogOptions(engine).size());
    std::vector<std::string> options;
    for (const ui::UiText& row : screen->options().rows)
    {
        options.push_back(row.value);
    }
    const auto it = std::ranges::find(options, text);
    std::string shown;
    for (const std::string& o : options)
    {
        shown += o + " | ";
    }
    REQUIRE_MESSAGE(it != options.end(), "not in the menu: ", std::string(text), " (menu: ", shown, ")");
    auto* dialog = dynamic_cast<ui::DialogScreen*>(engine.screens().find("dialog"));
    REQUIRE(dialog != nullptr);
    const auto wanted = static_cast<usize>(it - options.begin());
    for (int i = 0; i < 20 && dialog->options().selected() != wanted; ++i)
    {
        tap(engine, dialog->options().selected() < wanted ? SDL_SCANCODE_DOWN : SDL_SCANCODE_UP);
    }
    REQUIRE(dialog->options().selected() == wanted);
    tap(engine, SDL_SCANCODE_RETURN);
}

/// The action key on the NPC in focus: the dialogue starts once the hero stands (after turning).
void startTalking(Engine& engine)
{
    for (int attempt = 0; attempt < 5 && !engine.inDialog(); ++attempt)
    {
        tap(engine, SDL_SCANCODE_LCTRL); // action (a second press would choose in the menu: only if none)
        runUntil(engine, [&] { return engine.inDialog(); }, 20);
    }
    REQUIRE(engine.inDialog());
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("dialog") != nullptr; }, 120));
}

/// Talks to the NPC in focus with the action key, chooses the topics in order and "Ende" (`end`).
void talk(Engine& engine, std::initializer_list<std::string_view> path, std::string_view end = "Ende")
{
    startTalking(engine);
    for (const std::string_view step : path)
    {
        choose(engine, step);
    }
    choose(engine, end);
    REQUIRE(runUntil(engine, [&] { return !engine.inDialog(); }, 300));
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("dialog") == nullptr; }, 30));
}

/// In an item screen: switches tabs (E) until `item` is on the side asked for, then moves the selection
/// there.
void selectItem(Engine& engine, ui::ItemScreen& screen, std::string_view item, bool other)
{
    const auto index = [&]() -> std::optional<usize>
    {
        const auto& items = other ? screen.otherItems() : screen.heroItems();
        const auto it = std::ranges::find(items, item, &ui::ItemEntry::item);
        return it == items.end() ? std::nullopt : std::optional<usize>(it - items.begin());
    };
    for (usize i = 0; i <= screen.tabs().size() && !index(); ++i)
    {
        tap(engine, SDL_SCANCODE_E); // ui_next_tab
    }
    const auto wanted = index();
    REQUIRE_MESSAGE(wanted.has_value(), "not in the screen: ", std::string(item));
    ui::Grid& grid = other ? screen.otherGrid() : screen.heroGrid();
    if (screen.selectedInOther() != other)
    {
        tap(engine, other ? SDL_SCANCODE_LEFT : SDL_SCANCODE_RIGHT); // across to the other side
    }
    for (int i = 0; i < 30 && (grid.selected() != *wanted || screen.selectedInOther() != other); ++i)
    {
        const usize columns = std::max<usize>(grid.columns, 1);
        const usize row = grid.selected() / columns;
        const usize column = grid.selected() % columns;
        tap(engine, row < *wanted / columns      ? SDL_SCANCODE_DOWN
                    : row > *wanted / columns    ? SDL_SCANCODE_UP
                    : column < *wanted % columns ? SDL_SCANCODE_RIGHT
                                                 : SDL_SCANCODE_LEFT);
    }
    REQUIRE(screen.selected() != nullptr);
    REQUIRE(screen.selected()->item == item);
}

/// The game as it starts: the main menu (over the camp instead of Leonberg: faster), German, own user config.
EngineConfig gameConfig(const std::filesystem::path& user)
{
    auto settings = Config::load(fs::fromUtf8(G7_GAME_CONFIG));
    REQUIRE(settings.ok());
    const Config defaults = settings.value();
    Config merged = std::move(settings).value();
    merged.set<std::string>("menu.world", "testworld/camp.g7world"); // Leonberg is the menu-flight test's
    merged.set<std::string>("menu.flight", "");
    merged.set<std::string>("ui.language", "de");
    std::filesystem::remove(user);
    EngineConfig config;
    config.window.size = {640, 360};
    config.render = false; // the offscreen driver has no OpenGL; drawing is the render_gpu suite's
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.mainMenu = true;
    config.startTime = "12:00";
    config.defaultSettings = defaults;
    config.userConfig = user;
    config.settings = merged;
    return config;
}

/// Main menu -> New game ("Neues Spiel" has the focus): the camp behind the loading screen, the hero at its
/// start.
void newGame(Engine& engine)
{
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("menu") != nullptr; }));
    tap(engine, SDL_SCANCODE_RETURN);
    REQUIRE(runUntil(engine, [&] { return !engine.loading() && engine.player() != nullptr; }, 900));
    frames(engine, 3);
    REQUIRE_FALSE(engine.inMainMenu());
    REQUIRE(engine.screens().empty());
    run(engine, "Story.met_gate_guard = true"); // his greeting walks up to the hero: test_engine_dialog.cpp
}
} // namespace

TEST_CASE("M14 DoD: the vertical slice played through the game UI with keys only, without ImGui")
{
    const std::filesystem::path user = std::filesystem::temp_directory_path() / "g7_slice_user_config.toml";
    Engine engine(gameConfig(user));
    REQUIRE(engine.init().ok());
    REQUIRE(engine.gameUi() != nullptr);
    newGame(engine);
    const u64 errorsBefore = engine.scripts()->callErrors();

    // Dialogue and quest: the guard has work - the farmer woman needs help (diary entry, message).
    approach(engine, placeNpc(engine, "npc_gate_guard"), "Torwache");
    talk(engine, {"Gibt es hier Arbeit?", "Dann gehe ich gleich zu ihr."});
    CHECK(run(engine, "quest_status('quest_farm_work')").asString() == "running");
    CHECK(std::ranges::any_of(engine.screenMessages().messages(),
                              [](const auto& m) { return m.text == "Neuer Auftrag: Arbeit im Lager"; }));

    // The errand: the lunch from her to the woodcutter, 10 Gulden.
    approach(engine, placeNpc(engine, "npc_farmer_woman"), "Bäuerin");
    talk(engine, {"Die Wache schickt mich. Du brauchst Hilfe?"});
    CHECK(engine.hero()->itemCount("it_lunch") == 1);
    approach(engine, placeNpc(engine, "npc_woodcutter"), "Holzfäller");
    talk(engine, {"Die Bäuerin schickt dir dein Essen."});
    CHECK(run(engine, "quest_status('quest_lunch')").asString() == "success");
    const u32 gulden = engine.hero()->itemCount("it_gulden");
    CHECK(gulden >= 10);

    // Trading with the old man: an apple from his wares (one piece: the quantity starts at 1, F5).
    approach(engine, placeNpc(engine, "npc_old_man"), "Alter Mann");
    startTalking(engine);
    choose(engine, "Zeig mir, was du hast.");
    for (int i = 0; i < 40 && engine.screens().find("trade") == nullptr; ++i)
    {
        tap(engine, SDL_SCANCODE_RETURN); // his line
    }
    auto* trade = dynamic_cast<ui::ItemScreen*>(engine.screens().find("trade"));
    REQUIRE(trade != nullptr);
    frames(engine, 2);
    const u32 apples = engine.hero()->itemCount("it_apple");
    selectItem(engine, *trade, "it_apple", true);
    tap(engine, SDL_SCANCODE_RETURN);
    if (trade->quantityOpen())
    {
        CHECK(trade->quantity() == 1);
        tap(engine, SDL_SCANCODE_RETURN);
    }
    CHECK(engine.hero()->itemCount("it_apple") == apples + 1);
    CHECK(engine.hero()->itemCount("it_gulden") < gulden);
    tap(engine, SDL_SCANCODE_ESCAPE); // leaves the trade, the dialogue goes on
    REQUIRE(runUntil(engine, [&] { return !engine.trading(); }, 60));
    choose(engine, "Ende");
    REQUIRE(runUntil(engine, [&] { return !engine.inDialog() && engine.screens().empty(); }, 300));

    // Inventory: equip a sword (Enter on it), eat an apple (Enter on it), Tab closes.
    run(engine, "give_item('it_sword_old')");
    tap(engine, SDL_SCANCODE_TAB);
    auto* inventory = dynamic_cast<ui::ItemScreen*>(engine.screens().find("inventory"));
    REQUIRE(inventory != nullptr);
    frames(engine, 2);
    selectItem(engine, *inventory, "it_sword_old", false);
    tap(engine, SDL_SCANCODE_RETURN);
    CHECK(engine.hero()->equipped(gameplay::EquipSlot::Melee) == "it_sword_old");
    const u32 applesBefore = engine.hero()->itemCount("it_apple");
    selectItem(engine, *inventory, "it_apple", false);
    tap(engine, SDL_SCANCODE_RETURN);
    REQUIRE(runUntil(engine, [&] { return engine.hero()->itemCount("it_apple") == applesBefore - 1; }, 300));
    tap(engine, SDL_SCANCODE_TAB);
    REQUIRE(runUntil(engine, [&] { return engine.screens().empty(); }, 30));

    // The chest: action key on it, Enter takes the first stack (all of it), Esc leaves.
    const auto chest = engine.findMob("LAGER_TRUHE");
    REQUIRE(chest.has_value());
    const auto chestInfo = engine.mobInfo(*chest);
    REQUIRE(chestInfo.has_value());
    approach(engine, chestInfo->position, chestInfo->name);
    tap(engine, SDL_SCANCODE_LCTRL);
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("container") != nullptr; }, 300));
    frames(engine, 2);
    auto* container = dynamic_cast<ui::ItemScreen*>(engine.screens().find("container"));
    REQUIRE(container != nullptr);
    REQUIRE_FALSE(container->otherItems().empty());
    const ui::ItemEntry first = *container->selected();
    const u32 before = engine.hero()->itemCount(first.item);
    tap(engine, SDL_SCANCODE_RETURN);
    if (container->quantityOpen())
    {
        tap(engine, SDL_SCANCODE_RETURN);
    }
    CHECK(engine.hero()->itemCount(first.item) == before + first.count);
    tap(engine, SDL_SCANCODE_ESCAPE);
    REQUIRE(runUntil(engine, [&] { return !engine.mobPhase() && engine.screens().empty(); }, 300));

    // A fight: a (weakened) wolf; the sword drawn with Space, blows with Ctrl + Up until it is dead.
    run(engine, "Story.met_gate_guard = true");
    const Vec3 wolfAt = placeNpc(engine, "mon_wolf");
    run(engine, "npc_set_stat('mon_wolf', 'hp', 1)");
    approach(engine, wolfAt, "Wolf");
    tap(engine, SDL_SCANCODE_SPACE); // draw_weapon
    REQUIRE(runUntil(engine, [&] { return run(engine, "player_weapon()").asString() != "none"; }, 120));
    const auto wolfDown = [&]
    {
        const std::string state(run(engine, "fight_state('mon_wolf')").asString());
        return state == "dead" || state == "down";
    };
    key(engine, SDL_SCANCODE_LCTRL, true);
    for (int i = 0; i < 40 && !wolfDown(); ++i)
    {
        const auto id = engine.npcByInstance("mon_wolf");
        const auto at = id ? engine.creaturePosition(*id) : std::nullopt;
        const auto focus = engine.focus();
        if (at && (!focus || focus->name != "Wolf")) // it moved: turn towards it (Ctrl + left is a side blow)
        {
            key(engine, SDL_SCANCODE_LCTRL, false);
            frames(engine);
            key(engine, SDL_SCANCODE_LEFT, true);
            runUntil(engine, [&] { return engine.focus() && engine.focus()->name == "Wolf"; }, 120);
            key(engine, SDL_SCANCODE_LEFT, false);
            key(engine, SDL_SCANCODE_LCTRL, true);
            frames(engine);
        }
        tap(engine, SDL_SCANCODE_UP); // with Ctrl held: a blow
        frames(engine, 20);
    }
    key(engine, SDL_SCANCODE_LCTRL, false);
    frames(engine);
    CHECK(wolfDown());
    tap(engine, SDL_SCANCODE_SPACE); // the sword away

    // The diary (N): the quests with their entries; Esc closes it without pausing.
    tap(engine, SDL_SCANCODE_N);
    REQUIRE(engine.diaryOpen());
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("diary") != nullptr; }, 10));
    const Engine::DiaryData diary = engine.diaryData();
    CHECK(std::ranges::any_of(diary.running, [](const auto& q) { return q.name == "Arbeit im Lager"; }));
    CHECK(
        std::ranges::any_of(diary.done, [](const auto& q) { return q.name == "Essen für den Holzfäller"; }));
    tap(engine, SDL_SCANCODE_ESCAPE);
    CHECK_FALSE(engine.diaryOpen());
    CHECK_FALSE(engine.paused());

    // Pause (Esc): the menu stops the simulation; Esc goes back into the game.
    REQUIRE(runUntil(engine, [&] { return engine.screens().empty(); }, 10));
    tap(engine, SDL_SCANCODE_ESCAPE);
    REQUIRE(engine.screens().find("menu") != nullptr);
    CHECK(engine.screens().pausesGame());
    const u64 ticks = engine.simulationTicks();
    frames(engine, 3);
    CHECK(engine.simulationTicks() == ticks);
    tap(engine, SDL_SCANCODE_ESCAPE);
    CHECK(engine.screens().empty());
    CHECK(engine.scripts()->callErrors() == errorsBefore);
    std::filesystem::remove(user);
}

TEST_CASE("M14 DoD: the language switched to English in the options, the camp in English")
{
    const std::filesystem::path user =
        std::filesystem::temp_directory_path() / "g7_slice_en_user_config.toml";
    Engine engine(gameConfig(user));
    REQUIRE(engine.init().ok());
    REQUIRE(runUntil(engine, [&] { return engine.screens().find("menu") != nullptr; }));
    CHECK(engine.gameUi()->strings().get("UI_MENU_NEW_GAME") == "Neues Spiel");

    // Options (Down, Down past the greyed-out "Load"), page "Game" (three tabs on), language: Right.
    tap(engine, SDL_SCANCODE_DOWN);
    tap(engine, SDL_SCANCODE_DOWN);
    tap(engine, SDL_SCANCODE_RETURN);
    auto* options = dynamic_cast<ui::OptionsScreen*>(engine.screens().find("options"));
    REQUIRE(options != nullptr);
    for (int i = 0; i < 3; ++i)
    {
        tap(engine, SDL_SCANCODE_PAGEDOWN);
    }
    CHECK(options->page() == 3);
    tap(engine, SDL_SCANCODE_RIGHT);
    CHECK(engine.settings().get<std::string>("ui.language", "") == "en");
    CHECK(engine.gameUi()->strings().get("UI_MENU_NEW_GAME") == "New Game");
    CHECK(engine.uiText("UI_DIALOG_END") == "End");
    tap(engine, SDL_SCANCODE_ESCAPE); // saved: only what differs from the defaults
    auto saved = Config::load(user);
    REQUIRE(saved.ok());
    CHECK(saved.value().get<std::string>("ui.language", "") == "en");
    tap(engine, SDL_SCANCODE_UP);
    tap(engine, SDL_SCANCODE_UP);
    newGame(engine);

    // The guard in English: his name in the focus, the topics and the message; the game data stays German.
    approach(engine, placeNpc(engine, "npc_gate_guard"), "Gate Guard");
    talk(engine, {"Is there any work around here?", "Then I'll go see her right away."}, "End");
    CHECK(run(engine, "quest_status('quest_farm_work')").asString() == "running");
    CHECK(std::ranges::any_of(engine.screenMessages().messages(),
                              [](const auto& m) { return m.text == "New task: Work in the Camp"; }));
    const Engine::DiaryData diary = engine.diaryData();
    CHECK(diary.chapter == "Chapter 1");
    REQUIRE_FALSE(diary.running.empty());
    CHECK(diary.running[0].name == "Work in the Camp");
    CHECK(diary.running[0].entries[0].starts_with("Day 1, 12:"));
    CHECK(run(engine, "Story.quests.quest_farm_work.entries[1].text").asString() ==
          "Die Torwache meint, die Bäuerin am Feld brauche Hilfe."); // stored in German (saved games)

    // A notice of the engine: the magic key without a rune or scroll.
    tap(engine, SDL_SCANCODE_1); // draw_magic
    CHECK(engine.lastNotice() == "No rune and no scroll equipped.");
    std::filesystem::remove(user);
}

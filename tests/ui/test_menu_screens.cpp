// Main menu, options and loading screen of M14 part F without a GPU: option steps and texts, assigning keys
// with conflicts, the menu's disabled entries and going back, the options' pages, values and key capture.

#include <g7/platform/Actions.hpp>
#include <g7/platform/Input.hpp>
#include <g7/ui/MenuScreens.hpp>

#include <doctest/doctest.h>

#include <map>
#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::ui;
using platform::Action;
using platform::Key;

namespace
{
const Rect kScreen{Vec2(0.0f), Vec2(1920.0f, 1080.0f)};

UiInput step(NavDir dir)
{
    UiInput in;
    in.nav = dir;
    return in;
}

UiInput accept()
{
    UiInput in;
    in.accept = true;
    return in;
}

UiInput back()
{
    UiInput in;
    in.back = true;
    in.key = Key::Escape;
    return in;
}

UiInput keyPress(Key key)
{
    UiInput in;
    in.key = key;
    return in;
}

UiInput tab(int direction)
{
    UiInput in;
    (direction < 0 ? in.prevTab : in.nextTab) = true;
    return in;
}

void run(Screen& screen, const UiInput& in)
{
    screen.update(&in, 0.016f, kScreen, 1.0f, nullptr);
}

void idle(Screen& screen)
{
    run(screen, UiInput{});
}
} // namespace

TEST_CASE("Options: steps of sliders, toggles and choices")
{
    const OptionDef volume{"audio.music", "UI_OPT_MUSIC", OptionKind::Slider, 0.0, 1.0, 0.05, {}};
    CHECK(std::get<f64>(stepOption(volume, 0.8, +1)) == doctest::Approx(0.85));
    CHECK(std::get<f64>(stepOption(volume, 0.8, -1)) == doctest::Approx(0.75));
    CHECK(std::get<f64>(stepOption(volume, 1.0, +1)) == doctest::Approx(1.0)); // clamped
    CHECK(std::get<f64>(stepOption(volume, 0.0, -1)) == doctest::Approx(0.0));
    CHECK(std::get<f64>(stepOption(volume, i64{1}, -1)) == doctest::Approx(0.95)); // "music = 1" in a config
    CHECK(optionText(volume, 0.8).value == "80 %");

    const OptionDef vsync{"window.vsync", "UI_OPT_VSYNC", OptionKind::Toggle, 0.0, 1.0, 0.0, {}};
    CHECK(std::get<bool>(stepOption(vsync, true, +1)) == false);
    CHECK(std::get<bool>(stepOption(vsync, false, -1)) == true);
    CHECK(optionText(vsync, true).value == "UI_ON");
    CHECK(optionText(vsync, true).isKey);

    const OptionDef fps{"window.max_fps",
                        "UI_OPT_MAX_FPS",
                        OptionKind::Choice,
                        0.0,
                        1.0,
                        0.0,
                        {{i64{0}, UiText::key("UI_UNLIMITED")},
                         {i64{60}, UiText::literal("60")},
                         {i64{144}, UiText::literal("144")}}};
    CHECK(std::get<i64>(stepOption(fps, i64{60}, +1)) == 144);
    CHECK(std::get<i64>(stepOption(fps, i64{144}, +1)) == 0); // cycles
    CHECK(std::get<i64>(stepOption(fps, i64{0}, -1)) == 144);
    CHECK(std::get<i64>(stepOption(fps, i64{75}, +1)) == 0); // set by hand: starts at the first
    CHECK(optionText(fps, i64{0}).value == "UI_UNLIMITED");
    CHECK(optionText(fps, i64{75}).value == "75"); // shown as it is
}

TEST_CASE("Options: assigning a key takes it from other actions of the group")
{
    platform::ActionMap map;
    map.bind(Action::Jump, Key::LeftAlt);
    map.bind(Action::Jump, platform::GamepadButton::South);
    map.bind(Action::MoveForward, Key::Up);
    map.bind(Action::MoveForward, Key::W);
    map.bind(Action::Sneak, Key::X);
    map.bind(Action::UiUp, Key::W); // menus: outside the group, keeps its key
    const Action group[] = {Action::Jump, Action::MoveForward, Action::Sneak};

    SUBCASE("a free key replaces the first key, the gamepad stays")
    {
        CHECK(assignKey(map, Action::Jump, Key::Space, group).empty());
        const auto jump = map.bindings(Action::Jump);
        REQUIRE(jump.size() == 2);
        CHECK(jump[0] == platform::InputBinding{Key::Space});
        CHECK(jump[1] == platform::InputBinding{platform::GamepadButton::South});
    }
    SUBCASE("a key in use moves, the other action keeps its second key")
    {
        const auto displaced = assignKey(map, Action::Sneak, Key::W, group);
        REQUIRE(displaced.size() == 1);
        CHECK(displaced[0] == Action::MoveForward);
        const auto forward = map.bindings(Action::MoveForward);
        REQUIRE(forward.size() == 1);
        CHECK(forward[0] == platform::InputBinding{Key::Up});
        REQUIRE(map.bindings(Action::Sneak).size() == 1);
        CHECK(map.bindings(Action::Sneak)[0] == platform::InputBinding{Key::W});
        CHECK(map.bindings(Action::UiUp)[0] == platform::InputBinding{Key::W});
    }
    SUBCASE("the action's own second key becomes its first")
    {
        CHECK(assignKey(map, Action::MoveForward, Key::W, group).empty());
        const auto forward = map.bindings(Action::MoveForward);
        REQUIRE(forward.size() == 1);
        CHECK(forward[0] == platform::InputBinding{Key::W});
    }
}

TEST_CASE("Menu: entries, disabled ones and going back")
{
    std::vector<std::string> chosen;
    int backs = 0;
    const std::vector<MenuEntry> entries{{"resume", "UI_MENU_RESUME", true},
                                         {"load", "UI_MENU_LOAD", false},
                                         {"options", "UI_MENU_OPTIONS", true}};
    SUBCASE("pause menu")
    {
        MenuScreen menu(entries, {[&](std::string_view id) { chosen.emplace_back(id); }, [&] { ++backs; }},
                        false);
        CHECK(menu.flags().pausesGame);
        CHECK(menu.flags().hidesHud);
        run(menu, accept());
        run(menu, step(NavDir::Down));
        run(menu, accept()); // greyed out: nothing
        run(menu, step(NavDir::Down));
        run(menu, accept());
        CHECK(chosen == std::vector<std::string>{"resume", "options"});
        run(menu, back());
        CHECK(backs == 1);
        CHECK_FALSE(menu.closed()); // the engine closes it
    }
    SUBCASE("main menu: going back does nothing")
    {
        MenuScreen menu(entries, {[&](std::string_view id) { chosen.emplace_back(id); }, {}}, true);
        run(menu, back());
        CHECK_FALSE(menu.closed());
        menu.select("options");
        CHECK(menu.entries().selected() == 2);
        CHECK(menu.entries().isDimmed(1));
    }
}

TEST_CASE("Options screen: pages, values, keys and leaving")
{
    std::map<std::string, OptionValue> values{{"audio.music", 0.8}, {"window.vsync", true}};
    std::map<std::string, std::string> keys{{"jump", "LeftAlt"}};
    int closes = 0;
    std::vector<OptionPage> pages{
        {"UI_OPTIONS_AUDIO",
         {{"audio.music", "UI_OPT_MUSIC", OptionKind::Slider, 0.0, 1.0, 0.05, {}},
          {"window.vsync", "UI_OPT_VSYNC", OptionKind::Toggle, 0.0, 1.0, 0.0, {}}}},
        {"UI_OPTIONS_CONTROLS", {{"jump", "UI_ACTION_JUMP", OptionKind::Binding, 0.0, 1.0, 0.0, {}}}}};
    OptionsScreen::Actions actions;
    actions.get = [&](const OptionDef& o) { return values[o.id]; };
    actions.set = [&](const OptionDef& o, const OptionValue& v) { values[o.id] = v; };
    actions.binding = [&](const OptionDef& o) { return keys[o.id]; };
    actions.assign = [&](const OptionDef& o, Key key)
    {
        keys[o.id] = std::string(platform::name(key));
        return std::string("assigned");
    };
    actions.close = [&] { ++closes; };
    OptionsScreen screen(pages, actions);
    idle(screen);
    REQUIRE(screen.options().values.size() == 2);
    CHECK(screen.options().values[0].value == "80 %");
    REQUIRE(screen.options().sliders.size() == 2);
    REQUIRE(screen.options().sliders[0].has_value());
    CHECK(*screen.options().sliders[0] == doctest::Approx(0.8f));
    CHECK_FALSE(screen.options().sliders[1].has_value()); // a toggle: text

    run(screen, step(NavDir::Right));
    CHECK(std::get<f64>(values["audio.music"]) == doctest::Approx(0.85));
    run(screen, step(NavDir::Left));
    run(screen, step(NavDir::Left));
    CHECK(std::get<f64>(values["audio.music"]) == doctest::Approx(0.75));
    run(screen, step(NavDir::Down));
    run(screen, accept()); // toggles
    CHECK(std::get<bool>(values["window.vsync"]) == false);

    run(screen, tab(+1));
    CHECK(screen.page() == 1);
    idle(screen);
    CHECK(screen.options().values[0].value == "LeftAlt");
    run(screen, accept());
    REQUIRE(screen.waiting() != nullptr);
    idle(screen);
    CHECK(screen.options().values[0].value == "UI_OPTIONS_PRESS_KEY");
    run(screen, back()); // Esc cancels the waiting, the screen stays
    CHECK(screen.waiting() == nullptr);
    CHECK_FALSE(screen.closed());
    CHECK(keys["jump"] == "LeftAlt");
    run(screen, accept());
    run(screen, keyPress(Key::Space));
    CHECK(keys["jump"] == "Space");
    CHECK(screen.message() == "assigned");
    CHECK(screen.waiting() == nullptr);

    run(screen, tab(+1)); // wraps
    CHECK(screen.page() == 0);
    CHECK(closes == 0);
    run(screen, back());
    CHECK(screen.closed());
    CHECK(closes == 1);
}

TEST_CASE("Loading screen: world name and progress, not closed by the player")
{
    LoadingScreen screen([] { return LoadingView{"Leonberg", 0.4f}; });
    CHECK(screen.flags().pausesGame);
    run(screen, back());
    CHECK_FALSE(screen.closed());
}

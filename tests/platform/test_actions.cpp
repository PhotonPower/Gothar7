#include <g7/platform/Actions.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string_view operands
#include <set>
#include <string>

using namespace g7;
using namespace g7::platform;

namespace
{
Config parse(std::string_view toml)
{
    auto config = Config::parse(toml);
    REQUIRE_MESSAGE(config.ok(), (config.ok() ? "" : config.error().message));
    return std::move(config).value();
}
} // namespace

TEST_CASE("Actions: names are unique snake_case and round-trip")
{
    std::set<std::string> names;
    for (usize i = 0; i < static_cast<usize>(Action::Count); ++i)
    {
        const auto action = static_cast<Action>(i);
        const std::string_view actionName = name(action);
        CHECK(names.insert(std::string(actionName)).second);
        CHECK(actionFromName(actionName) == action);
        CHECK(actionName.find(' ') == std::string_view::npos);
    }
    CHECK(actionFromName("DRAW_WEAPON") == Action::DrawWeapon);
    CHECK_FALSE(actionFromName("fly").has_value());
    CHECK(name(Action::Count) == "unknown");
}

TEST_CASE("Actions: bindings resolve keys, mouse and gamepad names")
{
    CHECK(bindingFromName("W") == InputBinding(Key::W));
    CHECK(bindingFromName("leftctrl") == InputBinding(Key::LeftCtrl));
    CHECK(bindingFromName("MouseLeft") == InputBinding(MouseButton::Left));
    CHECK(bindingFromName("PadSouth") == InputBinding(GamepadButton::South));
    CHECK_FALSE(bindingFromName("Banana").has_value());
    CHECK(name(InputBinding(GamepadButton::Start)) == "PadStart");
}

TEST_CASE("ActionMap: reads a scheme and skips invalid entries")
{
    const Config config = parse(R"(
[bindings.test]
move_forward = ["W", "Up", "PadDpadUp"]
action = ["LeftCtrl", "Banana"]
attack = ["MouseLeft"]
fly = ["F"]
jump = "Space"
)");
    const ActionMap map = ActionMap::fromConfig(config, "test");
    CHECK(map.bindings(Action::MoveForward).size() == 3);
    REQUIRE(map.bindings(Action::Action).size() == 1); // "Banana" skipped
    CHECK(map.bindings(Action::Action)[0] == InputBinding(Key::LeftCtrl));
    CHECK(map.bindings(Action::Attack).size() == 1);
    CHECK(map.bindings(Action::Jump).empty()); // not a list
    CHECK(map.bindings(Action::Count).empty());

    const ActionMap missing = ActionMap::fromConfig(config, "nope");
    CHECK(missing.bindings(Action::MoveForward).empty());
}

TEST_CASE("ActionMap: several inputs per action")
{
    ActionMap map;
    map.bind(Action::MoveForward, Key::W);
    map.bind(Action::MoveForward, Key::Up);
    map.bind(Action::MoveForward, Key::W); // duplicate ignored
    CHECK(map.bindings(Action::MoveForward).size() == 2);

    Input input;
    input.beginFrame();
    input.onKey(Key::W, true);
    CHECK(map.isDown(input, Action::MoveForward));
    CHECK(map.pressed(input, Action::MoveForward));

    input.beginFrame();
    input.onKey(Key::Up, true);
    CHECK(map.pressed(input, Action::MoveForward)); // second key pressed while first held

    // Releasing one of two held keys does not release the action.
    input.beginFrame();
    input.onKey(Key::W, false);
    CHECK(map.isDown(input, Action::MoveForward));
    CHECK_FALSE(map.released(input, Action::MoveForward));

    input.beginFrame();
    input.onKey(Key::Up, false);
    CHECK_FALSE(map.isDown(input, Action::MoveForward));
    CHECK(map.released(input, Action::MoveForward));

    map.clear(Action::MoveForward);
    CHECK(map.bindings(Action::MoveForward).empty());
}

TEST_CASE("ActionMap: mouse and gamepad bindings")
{
    ActionMap map;
    map.bind(Action::Attack, MouseButton::Left);
    map.bind(Action::Jump, GamepadButton::South);

    Input input;
    input.beginFrame();
    input.onMouseButton(MouseButton::Left, true);
    input.onGamepadConnected(true);
    input.onGamepadButton(GamepadButton::South, true);
    CHECK(map.pressed(input, Action::Attack));
    CHECK(map.pressed(input, Action::Jump));
    CHECK_FALSE(map.isDown(input, Action::Use));
}

TEST_CASE("ActionMap: writeTo and fromConfig round trip")
{
    ActionMap original;
    original.bind(Action::DrawWeapon, Key::Space);
    original.bind(Action::DrawWeapon, GamepadButton::North);
    original.bind(Action::Attack, MouseButton::Left);

    Config config;
    original.writeTo(config, "custom");
    CHECK(config.keys("bindings.custom").size() == static_cast<usize>(Action::Count));

    const ActionMap restored = ActionMap::fromConfig(config, "custom");
    for (usize i = 0; i < static_cast<usize>(Action::Count); ++i)
    {
        const auto action = static_cast<Action>(i);
        const auto a = original.bindings(action);
        const auto b = restored.bindings(action);
        REQUIRE(a.size() == b.size());
        for (usize j = 0; j < a.size(); ++j)
        {
            CHECK(a[j] == b[j]);
        }
    }
}

TEST_CASE("Shipped engine.toml: both schemes list every action with valid inputs")
{
    auto config = Config::load(fs::fromUtf8(G7_GAME_CONFIG_DIR "/engine.toml"));
    REQUIRE_MESSAGE(config.ok(), (config.ok() ? "" : config.error().message));
    const Config& settings = config.value();

    CHECK(settings.get<std::string>("input.scheme", "") == "classic");
    CHECK(settings.get<i64>("window.width", 0) > 0);

    for (const char* scheme : {"classic", "modern"})
    {
        CAPTURE(scheme);
        const std::string table = std::string("bindings.") + scheme;
        CHECK(settings.keys(table).size() == static_cast<usize>(Action::Count));
        for (usize i = 0; i < static_cast<usize>(Action::Count); ++i)
        {
            const std::string key = table + "." + std::string(name(static_cast<Action>(i)));
            CAPTURE(key);
            const auto inputs = settings.find<std::vector<std::string>>(key);
            REQUIRE(inputs.has_value());
            for (const std::string& input : *inputs)
            {
                CAPTURE(input);
                CHECK(bindingFromName(input).has_value());
            }
        }
    }

    const ActionMap classic = ActionMap::fromConfig(settings, "classic");
    CHECK(classic.bindings(Action::Action).size() >= 1);
    CHECK(classic.bindings(Action::Attack).size() == 1); // M11 (K1): the mouse as second assignment
    CHECK(classic.bindings(Action::Parry).size() == 1);
    CHECK(classic.bindings(Action::Use).empty());
    const ActionMap modern = ActionMap::fromConfig(settings, "modern");
    CHECK(modern.bindings(Action::Attack).size() >= 1);
    CHECK(modern.bindings(Action::Use).size() >= 1);
}

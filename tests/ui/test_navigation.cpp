// UI input (M14 part B): ui_* actions, repeat of held directions, stick, mouse, last device; spatial focus
// search.

#include <g7/core/Config.hpp>
#include <g7/platform/Actions.hpp>
#include <g7/platform/Input.hpp>
#include <g7/ui/Navigation.hpp>

#include <doctest/doctest.h>

#include <array>

using namespace g7;
using namespace g7::ui;
using platform::GamepadAxis;
using platform::GamepadButton;
using platform::Key;

namespace
{
platform::ActionMap uiActions()
{
    auto config = Config::parse(R"(
[bindings.test]
ui_up = ["Up", "PadDpadUp"]
ui_down = ["Down", "PadDpadDown"]
ui_left = ["Left"]
ui_right = ["Right"]
ui_accept = ["Enter", "PadSouth"]
ui_back = ["Escape", "PadEast"]
ui_prev_tab = ["Q"]
ui_next_tab = ["E"]
)");
    REQUIRE(config);
    return platform::ActionMap::fromConfig(config.value(), "test");
}
} // namespace

TEST_CASE("Navigation: a held direction steps at once, then repeats")
{
    const platform::ActionMap actions = uiActions();
    platform::Input input;
    NavRepeat repeat{.delay = 0.3f, .rate = 0.1f};

    input.beginFrame();
    input.onKey(Key::Down, true);
    CHECK(buildUiInput(input, actions, repeat, 0.016f).nav == NavDir::Down);
    input.beginFrame();
    CHECK(buildUiInput(input, actions, repeat, 0.2f).nav == NavDir::None); // still waiting (0.1 left)
    input.beginFrame();
    CHECK(buildUiInput(input, actions, repeat, 0.15f).nav == NavDir::Down); // the delay is over
    input.beginFrame();
    CHECK(buildUiInput(input, actions, repeat, 0.04f).nav == NavDir::None);
    input.beginFrame();
    CHECK(buildUiInput(input, actions, repeat, 0.06f).nav == NavDir::Down); // every 0.1 s
    input.beginFrame();
    input.onKey(Key::Down, false);
    CHECK(buildUiInput(input, actions, repeat, 0.016f).nav == NavDir::None);
    input.beginFrame();
    input.onKey(Key::Up, true);
    CHECK(buildUiInput(input, actions, repeat, 0.016f).nav == NavDir::Up); // a new direction: at once
}

TEST_CASE("Navigation: accept, back, tabs and the device used last")
{
    const platform::ActionMap actions = uiActions();
    platform::Input input;
    NavRepeat repeat;

    input.beginFrame();
    input.onKey(Key::Enter, true);
    UiInput in = buildUiInput(input, actions, repeat, 0.016f);
    CHECK(in.accept);
    CHECK_FALSE(in.back);
    CHECK(in.device == InputDevice::Keyboard);

    input.beginFrame();
    input.onGamepadConnected(true);
    input.onGamepadButton(GamepadButton::East, true);
    in = buildUiInput(input, actions, repeat, 0.016f);
    CHECK(in.back);
    CHECK(in.device == InputDevice::Gamepad);

    input.beginFrame();
    input.onKey(Key::E, true);
    in = buildUiInput(input, actions, repeat, 0.016f);
    CHECK(in.nextTab);
    CHECK_FALSE(in.prevTab);
    CHECK(in.device == InputDevice::Keyboard);
}

TEST_CASE("Navigation: the left stick navigates beyond its threshold")
{
    const platform::ActionMap actions = uiActions();
    platform::Input input;
    NavRepeat repeat;
    input.beginFrame();
    input.onGamepadConnected(true);
    input.onGamepadAxis(GamepadAxis::LeftY, 0.3f); // too little
    CHECK(buildUiInput(input, actions, repeat, 0.016f).nav == NavDir::None);
    input.beginFrame();
    input.onGamepadAxis(GamepadAxis::LeftY, 0.9f); // +Y is up
    UiInput in = buildUiInput(input, actions, repeat, 0.016f);
    CHECK(in.nav == NavDir::Up);
    CHECK(in.device == InputDevice::Gamepad);
    input.beginFrame();
    input.onGamepadAxis(GamepadAxis::LeftY, 0.0f);
    input.onGamepadAxis(GamepadAxis::LeftX, -0.8f);
    CHECK(buildUiInput(input, actions, repeat, 0.016f).nav == NavDir::Left);
}

TEST_CASE("Navigation: the mouse in framebuffer pixels")
{
    const platform::ActionMap actions = uiActions();
    platform::Input input;
    NavRepeat repeat;
    input.beginFrame();
    input.onMouseMotion(Vec2(100.0f, 50.0f), Vec2(3.0f, 0.0f));
    input.onMouseButton(platform::MouseButton::Left, true);
    input.onWheel(-2.0f);
    const UiInput in = buildUiInput(input, actions, repeat, 0.016f, Vec2(2.0f));
    CHECK(in.pointer == Vec2(200.0f, 100.0f)); // high-DPI: twice the pixels
    CHECK(in.pointerMoved);
    CHECK(in.click);
    CHECK(in.wheel == -2.0f);
    CHECK(in.device == InputDevice::Mouse);
}

TEST_CASE("Navigation: spatial search between widgets")
{
    // A row of three buttons and one below the middle one.
    const std::array<Rect, 4> rects = {Rect::fromSize(Vec2(0.0f, 0.0f), Vec2(100.0f, 40.0f)),
                                       Rect::fromSize(Vec2(120.0f, 0.0f), Vec2(100.0f, 40.0f)),
                                       Rect::fromSize(Vec2(240.0f, 0.0f), Vec2(100.0f, 40.0f)),
                                       Rect::fromSize(Vec2(120.0f, 100.0f), Vec2(100.0f, 40.0f))};
    CHECK(navigate(rects, 0, NavDir::Right) == 1u);
    CHECK(navigate(rects, 1, NavDir::Right) == 2u);
    CHECK_FALSE(navigate(rects, 2, NavDir::Right).has_value());
    CHECK(navigate(rects, 0, NavDir::Down) == 3u);
    CHECK(navigate(rects, 3, NavDir::Up) == 1u); // straight above beats the diagonal ones
    CHECK(navigate(rects, 2, NavDir::Left) == 1u);
    CHECK_FALSE(navigate(rects, 1, NavDir::Up).has_value());
    CHECK_FALSE(navigate(rects, 7, NavDir::Up).has_value());
    CHECK_FALSE(navigate(rects, 0, NavDir::None).has_value());
}

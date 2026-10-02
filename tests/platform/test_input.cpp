#include <g7/platform/Input.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string_view operands
#include <set>
#include <string>

using namespace g7;
using namespace g7::platform;

TEST_CASE("Input: key edges across frames")
{
    Input input;
    input.beginFrame();
    input.onKey(Key::W, true);
    CHECK(input.isDown(Key::W));
    CHECK(input.pressed(Key::W));
    CHECK_FALSE(input.released(Key::W));

    input.beginFrame();
    CHECK(input.isDown(Key::W));
    CHECK_FALSE(input.pressed(Key::W));

    input.onKey(Key::W, true); // repeated down without up: no new edge
    CHECK_FALSE(input.pressed(Key::W));

    input.beginFrame();
    input.onKey(Key::W, false);
    CHECK_FALSE(input.isDown(Key::W));
    CHECK(input.released(Key::W));

    input.beginFrame();
    CHECK_FALSE(input.released(Key::W));
}

TEST_CASE("Input: a tap within one frame is not lost")
{
    Input input;
    input.beginFrame();
    input.onKey(Key::Space, true);
    input.onKey(Key::Space, false);
    CHECK(input.pressed(Key::Space));
    CHECK(input.released(Key::Space));
    CHECK_FALSE(input.isDown(Key::Space));
}

TEST_CASE("Input: unknown and out-of-range keys are ignored")
{
    Input input;
    input.onKey(Key::Unknown, true);
    CHECK_FALSE(input.isDown(Key::Unknown));
    CHECK_FALSE(input.isDown(Key::Count));
    CHECK_FALSE(input.pressed(static_cast<Key>(9999)));
}

TEST_CASE("Input: mouse buttons, motion and wheel")
{
    Input input;
    input.beginFrame();
    input.onMouseButton(MouseButton::Left, true);
    input.onMouseMotion(Vec2(10, 20), Vec2(3, -1));
    input.onMouseMotion(Vec2(12, 25), Vec2(2, 5));
    input.onWheel(1.0f);
    input.onWheel(0.5f);

    CHECK(input.pressed(MouseButton::Left));
    CHECK(input.mousePosition() == Vec2(12, 25));
    CHECK(input.mouseDelta() == Vec2(5, 4));
    CHECK(input.wheelDelta() == doctest::Approx(1.5f));

    input.beginFrame();
    CHECK(input.isDown(MouseButton::Left));
    CHECK(input.mouseDelta() == Vec2(0.0f));
    CHECK(input.wheelDelta() == 0.0f);
    CHECK(input.mousePosition() == Vec2(12, 25)); // position persists
}

TEST_CASE("Input: releaseAll on focus loss")
{
    Input input;
    input.onKey(Key::LeftCtrl, true);
    input.onMouseButton(MouseButton::Right, true);
    input.onGamepadConnected(true);
    input.onGamepadButton(GamepadButton::South, true);
    input.onGamepadAxis(GamepadAxis::LeftX, 1.0f);

    input.beginFrame();
    input.releaseAll();
    CHECK_FALSE(input.isDown(Key::LeftCtrl));
    CHECK(input.released(Key::LeftCtrl));
    CHECK_FALSE(input.isDown(MouseButton::Right));
    CHECK_FALSE(input.isDown(GamepadButton::South));
    CHECK(input.axis(GamepadAxis::LeftX) == 0.0f);
    CHECK(input.gamepadConnected()); // still connected, just neutral
}

TEST_CASE("Input: stick dead zone is radial and rescaled")
{
    Input input;
    input.onGamepadConnected(true);
    REQUIRE(input.stickDeadzone() == doctest::Approx(0.2f));

    input.onGamepadAxis(GamepadAxis::LeftX, 0.15f);
    CHECK(input.axis(GamepadAxis::LeftX) == 0.0f);

    input.onGamepadAxis(GamepadAxis::LeftX, 1.0f);
    CHECK(input.axis(GamepadAxis::LeftX) == doctest::Approx(1.0f));
    CHECK(input.axis(GamepadAxis::LeftY) == 0.0f);

    // Just outside the dead zone starts near zero instead of jumping to 0.2.
    input.onGamepadAxis(GamepadAxis::LeftX, 0.25f);
    CHECK(input.axis(GamepadAxis::LeftX) == doctest::Approx(0.0625f));

    // Full diagonal is clamped to length 1.
    input.onGamepadAxis(GamepadAxis::LeftX, 1.0f);
    input.onGamepadAxis(GamepadAxis::LeftY, 1.0f);
    const Vec2 diagonal(input.axis(GamepadAxis::LeftX), input.axis(GamepadAxis::LeftY));
    CHECK(glm::length(diagonal) == doctest::Approx(1.0f));
    CHECK(diagonal.x == doctest::Approx(diagonal.y));

    // Right stick is independent.
    CHECK(input.axis(GamepadAxis::RightX) == 0.0f);

    // Values are clamped.
    input.onGamepadAxis(GamepadAxis::RightY, -3.0f);
    CHECK(input.axis(GamepadAxis::RightY) == doctest::Approx(-1.0f));

    input.setStickDeadzone(0.0f);
    input.onGamepadAxis(GamepadAxis::RightY, -0.1f);
    CHECK(input.axis(GamepadAxis::RightY) == doctest::Approx(-0.1f));
}

TEST_CASE("Input: triggers range 0..1 without dead zone")
{
    Input input;
    input.onGamepadAxis(GamepadAxis::RightTrigger, 0.1f);
    CHECK(input.axis(GamepadAxis::RightTrigger) == doctest::Approx(0.1f));
    input.onGamepadAxis(GamepadAxis::LeftTrigger, -0.5f);
    CHECK(input.axis(GamepadAxis::LeftTrigger) == 0.0f);
    input.onGamepadAxis(GamepadAxis::LeftTrigger, 2.0f);
    CHECK(input.axis(GamepadAxis::LeftTrigger) == 1.0f);
}

TEST_CASE("Input: disconnecting the gamepad resets its state")
{
    Input input;
    input.onGamepadConnected(true);
    input.onGamepadButton(GamepadButton::Start, true);
    input.onGamepadAxis(GamepadAxis::RightX, -1.0f);
    input.onGamepadAxis(GamepadAxis::LeftTrigger, 1.0f);

    input.beginFrame();
    input.onGamepadConnected(false);
    CHECK_FALSE(input.gamepadConnected());
    CHECK_FALSE(input.isDown(GamepadButton::Start));
    CHECK(input.released(GamepadButton::Start));
    CHECK(input.axis(GamepadAxis::RightX) == 0.0f);
    CHECK(input.axis(GamepadAxis::LeftTrigger) == 0.0f);
}

TEST_CASE("Input: names are unique and round-trip case-insensitively")
{
    std::set<std::string> all;
    for (usize i = 1; i < static_cast<usize>(Key::Count); ++i)
    {
        const auto key = static_cast<Key>(i);
        const std::string_view keyName = name(key);
        CHECK(all.insert(std::string(keyName)).second);
        CHECK(keyFromName(keyName) == key);
    }
    for (usize i = 0; i < static_cast<usize>(MouseButton::Count); ++i)
    {
        const auto button = static_cast<MouseButton>(i);
        CHECK(all.insert(std::string(name(button))).second);
        CHECK(mouseButtonFromName(name(button)) == button);
    }
    for (usize i = 0; i < static_cast<usize>(GamepadButton::Count); ++i)
    {
        const auto button = static_cast<GamepadButton>(i);
        CHECK(all.insert(std::string(name(button))).second);
        CHECK(gamepadButtonFromName(name(button)) == button);
    }

    CHECK(keyFromName("leftctrl") == Key::LeftCtrl);
    CHECK(keyFromName("w") == Key::W);
    CHECK(keyFromName("1") == Key::Num1);
    CHECK(keyFromName("Grave") == Key::Grave);
    CHECK(mouseButtonFromName("mouseleft") == MouseButton::Left);
    CHECK(gamepadButtonFromName("PADSOUTH") == GamepadButton::South);

    CHECK_FALSE(keyFromName("Unknown").has_value());
    CHECK_FALSE(keyFromName("NoSuchKey").has_value());
    CHECK_FALSE(keyFromName("MouseLeft").has_value()); // namespaces don't overlap
    CHECK_FALSE(mouseButtonFromName("Left").has_value());
    CHECK(name(Key::Count) == "Unknown");
}

#include <g7/platform/Actions.hpp>
#include <g7/platform/Input.hpp>
#include <g7/ui/Navigation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace g7::ui
{
namespace
{
using platform::Action;

/// Which kind of input pressed one of the UI actions this frame, if any.
std::optional<InputDevice> pressedDevice(const platform::Input& input, const platform::ActionMap& actions)
{
    for (const Action action :
         {Action::UiUp, Action::UiDown, Action::UiLeft, Action::UiRight, Action::UiAccept, Action::UiBack,
          Action::UiPrevTab, Action::UiNextTab, Action::UiDrop})
    {
        for (const platform::InputBinding& binding : actions.bindings(action))
        {
            if (std::visit([&](auto value) { return input.pressed(value); }, binding))
            {
                if (std::holds_alternative<platform::GamepadButton>(binding))
                {
                    return InputDevice::Gamepad;
                }
                return std::holds_alternative<platform::MouseButton>(binding) ? InputDevice::Mouse
                                                                              : InputDevice::Keyboard;
            }
        }
    }
    return std::nullopt;
}

NavDir stickDirection(const platform::Input& input)
{
    if (!input.gamepadConnected())
    {
        return NavDir::None;
    }
    const f32 x = input.axis(platform::GamepadAxis::LeftX);
    const f32 y = input.axis(platform::GamepadAxis::LeftY); // + = up
    if (std::max(std::abs(x), std::abs(y)) < kStickNavThreshold)
    {
        return NavDir::None;
    }
    if (std::abs(x) > std::abs(y))
    {
        return x > 0.0f ? NavDir::Right : NavDir::Left;
    }
    return y > 0.0f ? NavDir::Up : NavDir::Down;
}
} // namespace

UiInput buildUiInput(const platform::Input& input, const platform::ActionMap& actions, NavRepeat& repeat,
                     f32 deltaSeconds, Vec2 pointerScale)
{
    UiInput out;
    // The held direction: actions first, then the stick.
    NavDir held = NavDir::None;
    if (actions.isDown(input, Action::UiUp))
    {
        held = NavDir::Up;
    }
    else if (actions.isDown(input, Action::UiDown))
    {
        held = NavDir::Down;
    }
    else if (actions.isDown(input, Action::UiLeft))
    {
        held = NavDir::Left;
    }
    else if (actions.isDown(input, Action::UiRight))
    {
        held = NavDir::Right;
    }
    const NavDir stick = stickDirection(input);
    if (held == NavDir::None && stick != NavDir::None)
    {
        held = stick;
        if (repeat.held != stick)
        {
            repeat.device = InputDevice::Gamepad;
        }
    }
    if (held != repeat.held)
    {
        repeat.held = held;
        repeat.timer = repeat.delay;
        out.nav = held; // the first step at once
    }
    else if (held != NavDir::None)
    {
        repeat.timer -= deltaSeconds;
        if (repeat.timer <= 0.0f)
        {
            out.nav = held;
            repeat.timer += std::max(repeat.rate, 0.01f);
            repeat.timer = std::max(repeat.timer, 0.0f);
        }
    }

    out.accept = actions.pressed(input, Action::UiAccept);
    out.back = actions.pressed(input, Action::UiBack);
    out.prevTab = actions.pressed(input, Action::UiPrevTab);
    out.nextTab = actions.pressed(input, Action::UiNextTab);
    out.drop = actions.pressed(input, Action::UiDrop);
    if (const auto device = pressedDevice(input, actions))
    {
        repeat.device = *device;
    }

    for (u16 k = 1; k < static_cast<u16>(platform::Key::Count); ++k) // 0: Unknown
    {
        if (input.pressed(static_cast<platform::Key>(k)))
        {
            out.key = static_cast<platform::Key>(k);
            break;
        }
    }

    out.pointer = input.mousePosition() * pointerScale;
    const Vec2 delta = input.mouseDelta();
    out.pointerMoved = delta.x != 0.0f || delta.y != 0.0f;
    out.click = input.pressed(platform::MouseButton::Left);
    out.wheel = input.wheelDelta();
    if (out.pointerMoved || out.click || out.wheel != 0.0f)
    {
        repeat.device = InputDevice::Mouse;
    }
    out.device = repeat.device;
    return out;
}

std::optional<usize> navigate(std::span<const Rect> targets, usize current, NavDir dir) noexcept
{
    if (dir == NavDir::None || current >= targets.size())
    {
        return std::nullopt;
    }
    const Vec2 from = (targets[current].min + targets[current].max) * 0.5f;
    Vec2 axis(0.0f);
    switch (dir)
    {
    case NavDir::Up:
        axis = Vec2(0.0f, -1.0f);
        break;
    case NavDir::Down:
        axis = Vec2(0.0f, 1.0f);
        break;
    case NavDir::Left:
        axis = Vec2(-1.0f, 0.0f);
        break;
    case NavDir::Right:
        axis = Vec2(1.0f, 0.0f);
        break;
    case NavDir::None:
        break;
    }
    const Vec2 side(axis.y, -axis.x);
    std::optional<usize> best;
    f32 bestScore = std::numeric_limits<f32>::max();
    for (usize i = 0; i < targets.size(); ++i)
    {
        if (i == current)
        {
            continue;
        }
        const Vec2 to = (targets[i].min + targets[i].max) * 0.5f - from;
        const f32 along = glm::dot(to, axis);
        if (along <= 0.5f)
        {
            continue; // not on that side
        }
        const f32 score = along + 2.0f * std::abs(glm::dot(to, side));
        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    return best;
}
} // namespace g7::ui

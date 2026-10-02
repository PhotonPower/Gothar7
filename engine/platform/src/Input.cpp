#include <g7/core/StringUtil.hpp>
#include <g7/platform/Input.hpp>

#include <algorithm>
#include <cmath>

namespace g7::platform
{
namespace
{
// Same order as the enums. Mouse and gamepad names carry a prefix so a binding string
// ("Left" = arrow key, "MouseLeft", "PadSouth") always names exactly one input.
// clang-format off
constexpr std::array<std::string_view, static_cast<usize>(Key::Count)> kKeyNames = {
    "Unknown",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Escape", "Enter", "Space", "Tab", "Backspace", "Insert", "Delete", "Home", "End", "PageUp", "PageDown",
    "Up", "Down", "Left", "Right",
    "LeftShift", "RightShift", "LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt", "CapsLock",
    "Grave", "Minus", "Equals", "LeftBracket", "RightBracket", "Backslash", "Semicolon", "Apostrophe",
    "Comma", "Period", "Slash",
    "Keypad0", "Keypad1", "Keypad2", "Keypad3", "Keypad4", "Keypad5", "Keypad6", "Keypad7", "Keypad8", "Keypad9",
    "KeypadPlus", "KeypadMinus", "KeypadMultiply", "KeypadDivide", "KeypadEnter", "KeypadPeriod",
    "Pause", "PrintScreen",
};

constexpr std::array<std::string_view, static_cast<usize>(MouseButton::Count)> kMouseButtonNames = {
    "MouseLeft", "MouseRight", "MouseMiddle", "MouseX1", "MouseX2",
};

constexpr std::array<std::string_view, static_cast<usize>(GamepadButton::Count)> kGamepadButtonNames = {
    "PadSouth", "PadEast", "PadWest", "PadNorth", "PadLeftShoulder", "PadRightShoulder",
    "PadLeftStick", "PadRightStick", "PadBack", "PadStart",
    "PadDpadUp", "PadDpadDown", "PadDpadLeft", "PadDpadRight",
};
// clang-format on

template <typename Enum, usize N>
std::optional<Enum> fromName(const std::array<std::string_view, N>& names, std::string_view name) noexcept
{
    for (usize i = 0; i < N; ++i)
    {
        if (equalsIgnoreCase(names[i], name))
        {
            return static_cast<Enum>(i);
        }
    }
    return std::nullopt;
}

template <typename Enum>
constexpr usize index(Enum value) noexcept
{
    return static_cast<usize>(value);
}

template <typename Enum, usize N>
bool inRange(Enum value, const std::array<u8, N>&) noexcept
{
    return index(value) < N;
}
} // namespace

Input::Input() noexcept = default;

void Input::update(u8& state, bool down) noexcept
{
    if (down && !(state & kDown))
    {
        state = static_cast<u8>(state | kDown | kPressed);
    }
    else if (!down && (state & kDown))
    {
        state = static_cast<u8>((state & ~kDown) | kReleased);
    }
}

void Input::beginFrame() noexcept
{
    const auto clearEdges = [](auto& states)
    {
        for (u8& state : states)
        {
            state = static_cast<u8>(state & kDown);
        }
    };
    clearEdges(m_keys);
    clearEdges(m_mouseButtons);
    clearEdges(m_gamepadButtons);
    m_mouseDelta = Vec2(0.0f);
    m_text.clear();
    m_wheelDelta = 0.0f;
}

bool Input::isDown(Key key) const noexcept
{
    return inRange(key, m_keys) && (m_keys[index(key)] & kDown);
}
bool Input::pressed(Key key) const noexcept
{
    return inRange(key, m_keys) && (m_keys[index(key)] & kPressed);
}
bool Input::released(Key key) const noexcept
{
    return inRange(key, m_keys) && (m_keys[index(key)] & kReleased);
}

bool Input::isDown(MouseButton button) const noexcept
{
    return inRange(button, m_mouseButtons) && (m_mouseButtons[index(button)] & kDown);
}
bool Input::pressed(MouseButton button) const noexcept
{
    return inRange(button, m_mouseButtons) && (m_mouseButtons[index(button)] & kPressed);
}
bool Input::released(MouseButton button) const noexcept
{
    return inRange(button, m_mouseButtons) && (m_mouseButtons[index(button)] & kReleased);
}

bool Input::isDown(GamepadButton button) const noexcept
{
    return inRange(button, m_gamepadButtons) && (m_gamepadButtons[index(button)] & kDown);
}
bool Input::pressed(GamepadButton button) const noexcept
{
    return inRange(button, m_gamepadButtons) && (m_gamepadButtons[index(button)] & kPressed);
}
bool Input::released(GamepadButton button) const noexcept
{
    return inRange(button, m_gamepadButtons) && (m_gamepadButtons[index(button)] & kReleased);
}

f32 Input::axis(GamepadAxis axis) const noexcept
{
    if (index(axis) >= m_axes.size())
    {
        return 0.0f;
    }
    if (axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger)
    {
        return m_axes[index(axis)];
    }
    // Radial dead zone on the whole stick, so diagonals behave like straight directions.
    const bool left = axis == GamepadAxis::LeftX || axis == GamepadAxis::LeftY;
    const Vec2 stick = left ? Vec2(m_axes[index(GamepadAxis::LeftX)], m_axes[index(GamepadAxis::LeftY)])
                            : Vec2(m_axes[index(GamepadAxis::RightX)], m_axes[index(GamepadAxis::RightY)]);
    const f32 magnitude = glm::length(stick);
    if (magnitude <= m_deadzone || magnitude <= 0.0f)
    {
        return 0.0f;
    }
    const f32 scaled = std::min(1.0f, (magnitude - m_deadzone) / (1.0f - m_deadzone));
    const Vec2 result = stick / magnitude * scaled;
    return (axis == GamepadAxis::LeftX || axis == GamepadAxis::RightX) ? result.x : result.y;
}

void Input::setStickDeadzone(f32 deadzone) noexcept
{
    m_deadzone = std::clamp(deadzone, 0.0f, 0.95f);
}

void Input::onKey(Key key, bool down) noexcept
{
    if (key != Key::Unknown && inRange(key, m_keys))
    {
        update(m_keys[index(key)], down);
    }
}

void Input::onMouseButton(MouseButton button, bool down) noexcept
{
    if (inRange(button, m_mouseButtons))
    {
        update(m_mouseButtons[index(button)], down);
    }
}

void Input::onMouseMotion(Vec2 position, Vec2 delta) noexcept
{
    m_mousePosition = position;
    m_mouseDelta += delta;
}

void Input::onText(std::string_view utf8)
{
    m_text += utf8;
}

void Input::onWheel(f32 steps) noexcept
{
    m_wheelDelta += steps;
}

void Input::onGamepadButton(GamepadButton button, bool down) noexcept
{
    if (inRange(button, m_gamepadButtons))
    {
        update(m_gamepadButtons[index(button)], down);
    }
}

void Input::onGamepadAxis(GamepadAxis axis, f32 value) noexcept
{
    if (index(axis) >= m_axes.size())
    {
        return;
    }
    const bool trigger = axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
    m_axes[index(axis)] = std::clamp(value, trigger ? 0.0f : -1.0f, 1.0f);
}

void Input::onGamepadConnected(bool connected) noexcept
{
    m_gamepadConnected = connected;
    if (!connected)
    {
        for (u8& state : m_gamepadButtons)
        {
            update(state, false);
        }
        m_axes.fill(0.0f);
    }
}

void Input::releaseAll() noexcept
{
    const auto release = [](auto& states)
    {
        for (u8& state : states)
        {
            update(state, false);
        }
    };
    release(m_keys);
    release(m_mouseButtons);
    release(m_gamepadButtons);
    m_axes.fill(0.0f);
}

std::string_view name(Key key) noexcept
{
    return index(key) < kKeyNames.size() ? kKeyNames[index(key)] : kKeyNames[0];
}

std::string_view name(MouseButton button) noexcept
{
    return index(button) < kMouseButtonNames.size() ? kMouseButtonNames[index(button)]
                                                    : std::string_view("Unknown");
}

std::string_view name(GamepadButton button) noexcept
{
    return index(button) < kGamepadButtonNames.size() ? kGamepadButtonNames[index(button)]
                                                      : std::string_view("Unknown");
}

std::optional<Key> keyFromName(std::string_view keyName) noexcept
{
    const auto key = fromName<Key>(kKeyNames, keyName);
    return key == Key::Unknown ? std::nullopt : key;
}

std::optional<MouseButton> mouseButtonFromName(std::string_view buttonName) noexcept
{
    return fromName<MouseButton>(kMouseButtonNames, buttonName);
}

std::optional<GamepadButton> gamepadButtonFromName(std::string_view buttonName) noexcept
{
    return fromName<GamepadButton>(kGamepadButtonNames, buttonName);
}
} // namespace g7::platform

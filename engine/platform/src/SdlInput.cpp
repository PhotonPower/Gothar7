#include "SdlInput.hpp"

namespace g7::platform::sdl
{
namespace
{
Key offset(Key first, int distance) noexcept
{
    return static_cast<Key>(static_cast<int>(first) + distance);
}
} // namespace

Key toKey(SDL_Scancode scancode) noexcept
{
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
    {
        return offset(Key::A, scancode - SDL_SCANCODE_A);
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
    {
        return offset(Key::Num1, scancode - SDL_SCANCODE_1);
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
    {
        return offset(Key::F1, scancode - SDL_SCANCODE_F1);
    }
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
    {
        return offset(Key::Keypad1, scancode - SDL_SCANCODE_KP_1);
    }
    switch (scancode)
    {
    case SDL_SCANCODE_0:
        return Key::Num0;
    case SDL_SCANCODE_KP_0:
        return Key::Keypad0;
    case SDL_SCANCODE_ESCAPE:
        return Key::Escape;
    case SDL_SCANCODE_RETURN:
        return Key::Enter;
    case SDL_SCANCODE_SPACE:
        return Key::Space;
    case SDL_SCANCODE_TAB:
        return Key::Tab;
    case SDL_SCANCODE_BACKSPACE:
        return Key::Backspace;
    case SDL_SCANCODE_INSERT:
        return Key::Insert;
    case SDL_SCANCODE_DELETE:
        return Key::Delete;
    case SDL_SCANCODE_HOME:
        return Key::Home;
    case SDL_SCANCODE_END:
        return Key::End;
    case SDL_SCANCODE_PAGEUP:
        return Key::PageUp;
    case SDL_SCANCODE_PAGEDOWN:
        return Key::PageDown;
    case SDL_SCANCODE_UP:
        return Key::Up;
    case SDL_SCANCODE_DOWN:
        return Key::Down;
    case SDL_SCANCODE_LEFT:
        return Key::Left;
    case SDL_SCANCODE_RIGHT:
        return Key::Right;
    case SDL_SCANCODE_LSHIFT:
        return Key::LeftShift;
    case SDL_SCANCODE_RSHIFT:
        return Key::RightShift;
    case SDL_SCANCODE_LCTRL:
        return Key::LeftCtrl;
    case SDL_SCANCODE_RCTRL:
        return Key::RightCtrl;
    case SDL_SCANCODE_LALT:
        return Key::LeftAlt;
    case SDL_SCANCODE_RALT:
        return Key::RightAlt;
    case SDL_SCANCODE_CAPSLOCK:
        return Key::CapsLock;
    case SDL_SCANCODE_GRAVE:
        return Key::Grave;
    case SDL_SCANCODE_MINUS:
        return Key::Minus;
    case SDL_SCANCODE_EQUALS:
        return Key::Equals;
    case SDL_SCANCODE_LEFTBRACKET:
        return Key::LeftBracket;
    case SDL_SCANCODE_RIGHTBRACKET:
        return Key::RightBracket;
    case SDL_SCANCODE_BACKSLASH:
        return Key::Backslash;
    case SDL_SCANCODE_SEMICOLON:
        return Key::Semicolon;
    case SDL_SCANCODE_APOSTROPHE:
        return Key::Apostrophe;
    case SDL_SCANCODE_COMMA:
        return Key::Comma;
    case SDL_SCANCODE_PERIOD:
        return Key::Period;
    case SDL_SCANCODE_SLASH:
        return Key::Slash;
    case SDL_SCANCODE_KP_PLUS:
        return Key::KeypadPlus;
    case SDL_SCANCODE_KP_MINUS:
        return Key::KeypadMinus;
    case SDL_SCANCODE_KP_MULTIPLY:
        return Key::KeypadMultiply;
    case SDL_SCANCODE_KP_DIVIDE:
        return Key::KeypadDivide;
    case SDL_SCANCODE_KP_ENTER:
        return Key::KeypadEnter;
    case SDL_SCANCODE_KP_PERIOD:
        return Key::KeypadPeriod;
    case SDL_SCANCODE_PAUSE:
        return Key::Pause;
    case SDL_SCANCODE_PRINTSCREEN:
        return Key::PrintScreen;
    default:
        return Key::Unknown;
    }
}

std::optional<MouseButton> toMouseButton(Uint8 button) noexcept
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return std::nullopt;
    }
}

std::optional<GamepadButton> toGamepadButton(Uint8 button) noexcept
{
    switch (static_cast<SDL_GamepadButton>(button))
    {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return GamepadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST:
        return GamepadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST:
        return GamepadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return GamepadButton::North;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return GamepadButton::LeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return GamepadButton::RightShoulder;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return GamepadButton::LeftStick;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return GamepadButton::RightStick;
    case SDL_GAMEPAD_BUTTON_BACK:
        return GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_START:
        return GamepadButton::Start;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return GamepadButton::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return GamepadButton::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return GamepadButton::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return GamepadButton::DpadRight;
    default:
        return std::nullopt; // Guide, paddles, touchpad: reserved by the OS or unused
    }
}

std::optional<GamepadAxis> toGamepadAxis(Uint8 axis) noexcept
{
    switch (static_cast<SDL_GamepadAxis>(axis))
    {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return GamepadAxis::LeftX;
    case SDL_GAMEPAD_AXIS_LEFTY:
        return GamepadAxis::LeftY;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return GamepadAxis::RightX;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return GamepadAxis::RightY;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return GamepadAxis::LeftTrigger;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return GamepadAxis::RightTrigger;
    default:
        return std::nullopt;
    }
}

f32 normalizeAxis(GamepadAxis axis, Sint16 value) noexcept
{
    const f32 normalized =
        value >= 0 ? static_cast<f32>(value) / 32767.0f : static_cast<f32>(value) / 32768.0f;
    // SDL reports +Y as down; the engine uses +Y up (pushing forward is positive).
    if (axis == GamepadAxis::LeftY || axis == GamepadAxis::RightY)
    {
        return -normalized;
    }
    return normalized;
}
} // namespace g7::platform::sdl

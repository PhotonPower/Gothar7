#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace g7::platform
{
/// Physical keys (scancodes): WASD stays in place on AZERTY/QWERTZ. Names follow the US layout.
enum class Key : u16
{
    Unknown,
    // clang-format off
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Escape, Enter, Space, Tab, Backspace, Insert, Delete, Home, End, PageUp, PageDown,
    Up, Down, Left, Right,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt, CapsLock,
    Grave, Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe,
    Comma, Period, Slash,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadPlus, KeypadMinus, KeypadMultiply, KeypadDivide, KeypadEnter, KeypadPeriod,
    Pause, PrintScreen,
    // clang-format on
    Count
};

enum class MouseButton : u8
{
    Left,
    Right,
    Middle,
    X1,
    X2,
    Count
};

/// Positional names (South = A on Xbox, Cross on PlayStation).
enum class GamepadButton : u8
{
    South,
    East,
    West,
    North,
    LeftShoulder,
    RightShoulder,
    LeftStick,
    RightStick,
    Back,
    Start,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};

/// Sticks: -1..1 with +X right and **+Y up** (pushing forward is positive). Triggers: 0..1.
enum class GamepadAxis : u8
{
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count
};

/// Per-frame input state. Platform-independent: Window::pollEvents feeds it from SDL events,
/// tests feed it directly. Call beginFrame() before feeding the events of a new frame.
class Input
{
public:
    Input() noexcept;

    /// Clears pressed/released edges and per-frame deltas.
    void beginFrame() noexcept;

    [[nodiscard]] bool isDown(Key key) const noexcept;
    /// Went down this frame (also true if it was released again within the same frame).
    [[nodiscard]] bool pressed(Key key) const noexcept;
    [[nodiscard]] bool released(Key key) const noexcept;

    [[nodiscard]] bool isDown(MouseButton button) const noexcept;
    [[nodiscard]] bool pressed(MouseButton button) const noexcept;
    [[nodiscard]] bool released(MouseButton button) const noexcept;

    [[nodiscard]] bool isDown(GamepadButton button) const noexcept;
    [[nodiscard]] bool pressed(GamepadButton button) const noexcept;
    [[nodiscard]] bool released(GamepadButton button) const noexcept;

    /// Window coordinates (+Y down).
    [[nodiscard]] Vec2 mousePosition() const noexcept { return m_mousePosition; }
    /// Accumulated motion this frame in screen space (+Y down); also valid in relative mode.
    [[nodiscard]] Vec2 mouseDelta() const noexcept { return m_mouseDelta; }
    /// Accumulated wheel steps this frame (+ = away from the user).
    [[nodiscard]] f32 wheelDelta() const noexcept { return m_wheelDelta; }
    /// Text typed this frame (UTF-8, layout- and IME-aware); only while Window::setTextInput is on.
    [[nodiscard]] std::string_view text() const noexcept { return m_text; }

    [[nodiscard]] bool gamepadConnected() const noexcept { return m_gamepadConnected; }
    /// Stick values with radial dead zone applied and rescaled, so motion starts smoothly at 0.
    [[nodiscard]] f32 axis(GamepadAxis axis) const noexcept;
    /// Radius in 0..1 below which stick input counts as zero. Default 0.2.
    void setStickDeadzone(f32 deadzone) noexcept;
    [[nodiscard]] f32 stickDeadzone() const noexcept { return m_deadzone; }

    // --- Feeding (Window::pollEvents, tests) ---
    void onKey(Key key, bool down) noexcept;
    void onMouseButton(MouseButton button, bool down) noexcept;
    void onMouseMotion(Vec2 position, Vec2 delta) noexcept;
    void onWheel(f32 steps) noexcept;
    void onText(std::string_view utf8);
    void onGamepadButton(GamepadButton button, bool down) noexcept;
    /// Raw value: sticks -1..1 (+Y up), triggers 0..1; clamped.
    void onGamepadAxis(GamepadAxis axis, f32 value) noexcept;
    /// Disconnecting releases all gamepad buttons and centres the axes.
    void onGamepadConnected(bool connected) noexcept;
    /// Releases every key and button (focus loss), so nothing stays stuck.
    void releaseAll() noexcept;

private:
    static constexpr u8 kDown = 1;
    static constexpr u8 kPressed = 2;
    static constexpr u8 kReleased = 4;

    static void update(u8& state, bool down) noexcept;

    std::array<u8, static_cast<usize>(Key::Count)> m_keys{};
    std::array<u8, static_cast<usize>(MouseButton::Count)> m_mouseButtons{};
    std::array<u8, static_cast<usize>(GamepadButton::Count)> m_gamepadButtons{};
    std::array<f32, static_cast<usize>(GamepadAxis::Count)> m_axes{};
    Vec2 m_mousePosition{0.0f};
    Vec2 m_mouseDelta{0.0f};
    f32 m_wheelDelta = 0.0f;
    std::string m_text;
    f32 m_deadzone = 0.2f;
    bool m_gamepadConnected = false;
};

/// Stable names for configuration files (bindings), e.g. "W", "LeftCtrl", "Grave", "F5".
[[nodiscard]] std::string_view name(Key key) noexcept;
[[nodiscard]] std::string_view name(MouseButton button) noexcept;
[[nodiscard]] std::string_view name(GamepadButton button) noexcept;
/// Case-insensitive lookup; nullopt for unknown names.
[[nodiscard]] std::optional<Key> keyFromName(std::string_view name) noexcept;
[[nodiscard]] std::optional<MouseButton> mouseButtonFromName(std::string_view name) noexcept;
[[nodiscard]] std::optional<GamepadButton> gamepadButtonFromName(std::string_view name) noexcept;
} // namespace g7::platform

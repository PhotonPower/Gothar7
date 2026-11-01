#pragma once

// Input for the game UI (M14 part B): the ui_* actions, the left stick and the mouse become one UiInput per
// frame; held directions repeat. Spatial focus search between widgets.

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>
#include <g7/ui/Canvas.hpp>

#include <optional>
#include <span>

namespace g7::platform
{
class Input;
class ActionMap;
} // namespace g7::platform

namespace g7::ui
{
enum class NavDir : u8
{
    None,
    Up,
    Down,
    Left,
    Right,
};

/// The device used last; it decides which key hints a screen shows.
enum class InputDevice : u8
{
    Keyboard,
    Mouse,
    Gamepad,
};

struct UiInput
{
    NavDir nav = NavDir::None; ///< a step this frame (first press or repeat)
    bool accept = false;
    bool back = false;
    bool prevTab = false;
    bool nextTab = false;
    Vec2 pointer{-1.0f}; ///< mouse in framebuffer pixels
    bool pointerMoved = false;
    bool click = false; ///< left mouse button went down
    f32 wheel = 0.0f;   ///< steps, + = away from the user (scroll up)
    InputDevice device = InputDevice::Keyboard;
};

/// State between frames: the held direction and its repeat timer, the last device.
struct NavRepeat
{
    f32 delay = 0.35f; ///< seconds until a held direction repeats
    f32 rate = 0.08f;  ///< seconds between repeats
    NavDir held = NavDir::None;
    f32 timer = 0.0f;
    InputDevice device = InputDevice::Keyboard;
};

/// Directions from the stick count beyond this deflection.
inline constexpr f32 kStickNavThreshold = 0.5f;

/// Reads the ui_* actions, the left stick and the mouse. `pointerScale` turns window coordinates into
/// framebuffer pixels (high-DPI displays).
[[nodiscard]] UiInput buildUiInput(const platform::Input& input, const platform::ActionMap& actions,
                                   NavRepeat& repeat, f32 deltaSeconds, Vec2 pointerScale = Vec2(1.0f));

/// The next of `targets` from `current` in direction `dir`: candidates lie on that side (centre beyond the
/// current centre), the nearest wins with sideways distance counting double. nullopt: none there.
[[nodiscard]] std::optional<usize> navigate(std::span<const Rect> targets, usize current,
                                            NavDir dir) noexcept;
} // namespace g7::ui

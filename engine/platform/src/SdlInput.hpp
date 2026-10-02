#pragma once

// Internal: translation from SDL input codes to g7::platform input types.

#include <g7/platform/Input.hpp>

#include <SDL3/SDL.h>

#include <optional>

namespace g7::platform::sdl
{
[[nodiscard]] Key toKey(SDL_Scancode scancode) noexcept;
[[nodiscard]] std::optional<MouseButton> toMouseButton(Uint8 button) noexcept;
[[nodiscard]] std::optional<GamepadButton> toGamepadButton(Uint8 button) noexcept;
[[nodiscard]] std::optional<GamepadAxis> toGamepadAxis(Uint8 axis) noexcept;
/// SDL range (sticks -32768..32767 with +Y down, triggers 0..32767) to Input range (+Y up).
[[nodiscard]] f32 normalizeAxis(GamepadAxis axis, Sint16 value) noexcept;
} // namespace g7::platform::sdl

#pragma once

// Light editing (M16 part D, docs/modules/editor.md): presets for new light vobs, the preview hour of the
// editor and the light budget of E1 (render.md "Lichter je Bild") as text, tested without a window.

#include <g7/core/Types.hpp>
#include <g7/world/Components.hpp>

#include <span>
#include <string>
#include <string_view>

namespace g7::editor
{
struct LightPreset
{
    std::string_view name; ///< "fire", also the stem of the vob name (LIGHT_FIRE_01)
    world::LightSource light;
};

/// Fire, torch, candle, lamp and window (daylight): starting values a new light vob takes.
[[nodiscard]] std::span<const LightPreset> lightPresets() noexcept;
[[nodiscard]] const LightPreset* findLightPreset(std::string_view name) noexcept;

/// The preview hour 0 .. 24 as day time: hour and minute (24 becomes 23:59).
struct HourMinute
{
    u32 hour = 0;
    u32 minute = 0;
};
[[nodiscard]] HourMinute splitHour(f32 hour) noexcept;

/// "12 lights this frame (max 128), 340 in the world, 20 window lights" - the editor's hint (E1).
[[nodiscard]] std::string lightBudgetText(usize inFrame, u32 maxLights, usize worldLights,
                                          usize windowLights);
} // namespace g7::editor

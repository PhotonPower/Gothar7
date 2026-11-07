#include <g7/editor/LightEdit.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace g7::editor
{
namespace
{
world::LightSource light(Vec3 color, f32 range, f32 intensity, f32 flicker, bool daylight = false)
{
    world::LightSource l;
    l.color = color;
    l.range = range;
    l.intensity = intensity;
    l.flicker = flicker;
    l.daylight = daylight;
    return l;
}

const std::array<LightPreset, 5> kPresets = {{
    {"fire", light(Vec3(1.0f, 0.55f, 0.25f), 8.0f, 4.0f, 0.3f)},
    {"torch", light(Vec3(1.0f, 0.62f, 0.3f), 6.0f, 3.0f, 0.2f)},
    {"candle", light(Vec3(1.0f, 0.7f, 0.4f), 3.0f, 1.2f, 0.15f)},
    {"lamp", light(Vec3(1.0f, 0.8f, 0.55f), 5.0f, 2.0f, 0.0f)},
    {"window", light(Vec3(1.0f, 1.0f, 1.0f), 4.0f, 2.0f, 0.0f, true)},
}};
} // namespace

std::span<const LightPreset> lightPresets() noexcept
{
    return kPresets;
}

const LightPreset* findLightPreset(std::string_view name) noexcept
{
    const auto it = std::ranges::find(kPresets, name, &LightPreset::name);
    return it != kPresets.end() ? &*it : nullptr;
}

HourMinute splitHour(f32 hour) noexcept
{
    const f32 h = std::isfinite(hour) ? std::clamp(hour, 0.0f, 24.0f) : 12.0f;
    const u32 minutes = std::min(static_cast<u32>(std::lround(h * 60.0f)), 24u * 60u - 1u);
    return {minutes / 60u, minutes % 60u};
}

std::string lightBudgetText(usize inFrame, u32 maxLights, usize worldLights, usize windowLights)
{
    return std::format("{} light(s) this frame (max {}), {} in the world, {} window light(s){}", inFrame,
                       maxLights, worldLights, windowLights,
                       inFrame >= maxLights ? " - budget full, the farthest are left out" : "");
}
} // namespace g7::editor

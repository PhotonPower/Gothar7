#pragma once

// Mirroring window glass (M17, render.md "Spiegelnde Fenster"): materials marked `extras.glass` (contract
// with welt) are drawn as dark panes that mirror the sky by Fresnel, with the sun's glint and the bullseye or
// diamond panes of the time around 1700 (common/glass.glsl). The option [render] window_reflections chooses
// the stage: off = the plain material as before, low = sky (stage A); medium adds reflection probes (B) and
// high screen-space reflections (C) - until those parts exist, medium and high draw stage A.

#include <g7/asset/MeshData.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <optional>
#include <string_view>

namespace g7::render
{
enum class WindowReflections : u8
{
    Off,    ///< glass is drawn like any material (byte-equal to before)
    Low,    ///< stage A: sky and sun, Fresnel, panes
    Medium, ///< A + reflection probes (B) - the default
    High,   ///< A + B + screen-space reflections (C)
};
/// "off", "low", "medium", "high"; nullopt for anything else.
[[nodiscard]] std::optional<WindowReflections> parseWindowReflections(std::string_view text) noexcept;
[[nodiscard]] std::string_view windowReflectionsName(WindowReflections quality) noexcept;

/// Fresnel reflectance of the glass at the cosine between view and normal: Schlick with f0 0.04, capped at
/// 0.5 at grazing views (decision 1: discreet, about 4 % head-on). Same as common/glass.glsl.
inline constexpr f32 kGlassF0 = 0.04f;
inline constexpr f32 kGlassMaxFresnel = 0.5f;
[[nodiscard]] f32 glassFresnel(f32 cosTheta) noexcept;

/// What the shader gets of a glass material (Material::glass).
struct GlassLook
{
    bool enabled = false;
    asset::GlassPattern pattern = asset::GlassPattern::Bullseye;
    Vec3 tint = asset::kDefaultGlassTint;
};

/// M17 windows F1b (decision 4): at night the panes glow warm where a light burns behind them. In the rooms
/// of the world (indoor zones near the camera) that is their own lights (candle, hearth); every other window
/// row (house, storey and side) is lit by chance, by the share of people still awake at that hour. The glow
/// shows by how dark it is outside.
/// Share of the window rows still lit at `hours` (0..24, wraps): most in the evening, few after midnight,
/// some again before sunrise, none to speak of by day (then the night factor hides it anyway).
[[nodiscard]] f32 windowsAwake(f32 hours) noexcept;
/// How dark it is outside for the glow, 0 by day .. 1 at night, from the height of the sun (the y of the
/// direction towards it): fading in from a little above the horizon to a little below.
[[nodiscard]] f32 windowGlowNight(f32 sunHeight) noexcept;
} // namespace g7::render

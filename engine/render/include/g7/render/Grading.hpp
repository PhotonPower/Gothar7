#pragma once

// Colour grading (M17 part P2, render.md "Plan M17"): parameters instead of LUT files, applied in the post
// pass - white balance, saturation and contrast on the linear HDR colour before the tonemapping,
// lift/gamma/gain on the display colour after it, then a vignette. Values per time of day and zone come from
// data/environment.toml.

#include <g7/core/Math.hpp>

#include <optional>

namespace g7::render
{
struct Grading
{
    f32 temperature = 0.0f; ///< -1 cool (blue) .. +1 warm (orange)
    f32 tint = 0.0f;        ///< -1 green .. +1 magenta
    f32 saturation = 1.0f;  ///< 0 grey, 1 unchanged
    f32 contrast = 1.0f;    ///< around middle grey (0.18), 1 unchanged
    Vec3 lift{0.0f};        ///< raises the shadows (display colour)
    Vec3 gamma{1.0f};       ///< mid tones (display colour, > 1 brighter)
    Vec3 gain{1.0f};        ///< scales the highlights (display colour)
    f32 vignette = 0.0f;    ///< darkening towards the corners, 0 none
    friend bool operator==(const Grading&, const Grading&) = default;
};

/// Some fields of a grading (a zone in environment.toml); the rest stays.
struct GradingOverride
{
    std::optional<f32> temperature;
    std::optional<f32> tint;
    std::optional<f32> saturation;
    std::optional<f32> contrast;
    std::optional<Vec3> lift;
    std::optional<Vec3> gamma;
    std::optional<Vec3> gain;
    std::optional<f32> vignette;
};

[[nodiscard]] Grading mixGrading(const Grading& a, const Grading& b, f32 t) noexcept;
[[nodiscard]] Grading applyOverride(Grading base, const GradingOverride& over) noexcept;
/// The part before the tonemapping (linear HDR): white balance, saturation, contrast. Same as post.frag.
[[nodiscard]] Vec3 gradeLinear(const Vec3& color, const Grading& grading) noexcept;
/// The part after it (display colour 0..1): lift, gamma, gain. Same as post.frag.
[[nodiscard]] Vec3 gradeDisplay(const Vec3& color, const Grading& grading) noexcept;
/// Darkening factor at `uv` (0..1 over the screen) for the vignette strength.
[[nodiscard]] f32 vignetteFactor(Vec2 uv, f32 strength) noexcept;
} // namespace g7::render

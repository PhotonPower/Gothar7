#include <g7/render/Grading.hpp>

#include <algorithm>
#include <cmath>

namespace g7::render
{
namespace
{
constexpr Vec3 kLuma{0.2126f, 0.7152f, 0.0722f};
constexpr f32 kMiddleGrey = 0.18f;
} // namespace

Grading mixGrading(const Grading& a, const Grading& b, f32 t) noexcept
{
    Grading g;
    g.temperature = glm::mix(a.temperature, b.temperature, t);
    g.tint = glm::mix(a.tint, b.tint, t);
    g.saturation = glm::mix(a.saturation, b.saturation, t);
    g.contrast = glm::mix(a.contrast, b.contrast, t);
    g.lift = glm::mix(a.lift, b.lift, t);
    g.gamma = glm::mix(a.gamma, b.gamma, t);
    g.gain = glm::mix(a.gain, b.gain, t);
    g.vignette = glm::mix(a.vignette, b.vignette, t);
    return g;
}

Grading applyOverride(Grading base, const GradingOverride& over) noexcept
{
    base.temperature = over.temperature.value_or(base.temperature);
    base.tint = over.tint.value_or(base.tint);
    base.saturation = over.saturation.value_or(base.saturation);
    base.contrast = over.contrast.value_or(base.contrast);
    base.lift = over.lift.value_or(base.lift);
    base.gamma = over.gamma.value_or(base.gamma);
    base.gain = over.gain.value_or(base.gain);
    base.vignette = over.vignette.value_or(base.vignette);
    return base;
}

Vec3 gradeLinear(const Vec3& color, const Grading& g) noexcept
{
    // White balance: warmer = more red, less blue; tint = less green; brightness kept (luminance).
    Vec3 balance(1.0f + 0.2f * g.temperature, 1.0f - 0.1f * g.tint, 1.0f - 0.2f * g.temperature);
    balance /= glm::dot(balance, kLuma);
    Vec3 c = glm::max(color, Vec3(0.0f)) * balance;
    const f32 luma = glm::dot(c, kLuma);
    c = glm::max(glm::mix(Vec3(luma), c, g.saturation), Vec3(0.0f));
    // Contrast in log space around middle grey.
    for (int i = 0; i < 3; ++i)
    {
        c[i] = c[i] > 0.0f ? kMiddleGrey * std::pow(c[i] / kMiddleGrey, g.contrast) : 0.0f;
    }
    return c;
}

Vec3 gradeDisplay(const Vec3& color, const Grading& g) noexcept
{
    Vec3 c = glm::clamp(color, Vec3(0.0f), Vec3(1.0f));
    c = g.gain * c + g.lift * (Vec3(1.0f) - c);
    c = glm::clamp(c, Vec3(0.0f), Vec3(1.0f));
    for (int i = 0; i < 3; ++i)
    {
        c[i] = std::pow(c[i], 1.0f / std::max(g.gamma[i], 0.01f));
    }
    return c;
}

f32 vignetteFactor(Vec2 uv, f32 strength) noexcept
{
    const f32 d = glm::length((uv - Vec2(0.5f)) * Vec2(1.0f, 0.8f)) * 1.41421356f; // 1 at the corners
    const f32 t = std::clamp((d - 0.45f) / (1.0f - 0.45f), 0.0f, 1.0f);
    return 1.0f - strength * t * t * (3.0f - 2.0f * t);
}
} // namespace g7::render

#pragma once

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <array>
#include <vector>

namespace g7::render
{
/// Global lighting of a frame. Driven by the time of day from M4 (sky colours, sun path).
struct Environment
{
    Vec3 sunDirection{0.0f, 1.0f, 0.0f}; ///< Towards the sun (normalised by the renderer).
    Vec3 sunColor{1.0f};
    f32 sunIntensity = 1.0f;
    Vec3 ambientSky{0.2f};    ///< Ambient from above (hemisphere lighting).
    Vec3 ambientGround{0.1f}; ///< Ambient from below.
};

/// Point light (torch, campfire). Flicker etc. is game/world logic that changes these per frame.
struct PointLight
{
    Vec3 position{0.0f};
    f32 radius = 5.0f; ///< Influence ends exactly here.
    Vec3 color{1.0f};
    f32 intensity = 1.0f;
};

/// Smooth distance falloff, 1 at the light, exactly 0 at and beyond `radius`:
/// saturate(1 - (d/r)^4)^2 / (d^2 + 1). Same formula as common/lighting.glsl.
[[nodiscard]] f32 pointLightAttenuation(f32 distance, f32 radius) noexcept;

/// Point lights of a frame and the per-object selection of the forward renderer (render.md):
/// every object is lit by at most kMaxPerObject lights whose range touches its bounds.
class LightList
{
public:
    static constexpr u32 kMaxPerFrame = 256;
    static constexpr u32 kMaxPerObject = 8;

    void clear() noexcept;
    /// Lights beyond kMaxPerFrame are dropped (one warning per frame).
    void add(const PointLight& light);
    [[nodiscard]] const std::vector<PointLight>& lights() const noexcept { return m_lights; }

    /// Indices of the lights reaching `bounds`, nearest (to the box) first, at most kMaxPerObject.
    /// Deterministic: equal distances keep insertion order.
    void selectFor(const AABB& bounds, std::vector<u32>& out) const;

private:
    std::vector<PointLight> m_lights;
    bool m_warned = false;
};

/// std140 layout of the lighting uniform block (binding 0 in common/lighting.glsl).
struct GpuLighting
{
    Vec4 sunDirection;           ///< xyz
    Vec4 sunColor;               ///< rgb * intensity
    Vec4 ambientSky;             ///< rgb
    Vec4 ambientGround;          ///< rgb
    std::array<i32, 4> counts{}; ///< x = number of point lights
    std::array<Vec4, LightList::kMaxPerFrame> pointPositionRadius;
    std::array<Vec4, LightList::kMaxPerFrame> pointColor; ///< rgb * intensity
};
static_assert(sizeof(GpuLighting) == 5 * 16 + 2 * 16 * LightList::kMaxPerFrame, "must match std140");

[[nodiscard]] GpuLighting packLighting(const Environment& environment, const LightList& lights) noexcept;
} // namespace g7::render

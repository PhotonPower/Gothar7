#pragma once

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <array>
#include <span>
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
    /// Distance fog (render.md: strong, matching the sky at the horizon). Linear colour; no fog
    /// closer than fogStart, then exponential-squared with fogDensity (0 = off).
    Vec3 fogColor{0.08f, 0.04f, 0.033f};
    f32 fogStart = 30.0f;
    f32 fogDensity = 0.0f;
};

/// Sky of the background pass: colour from the horizon (= fog colour) to `zenith`, sun and moon discs,
/// stars. Linear colours.
struct Sky
{
    Vec3 zenith{0.04f, 0.03f, 0.05f};
    Vec3 sunDirection{0.0f, 1.0f, 0.0f}; ///< towards the sun (below the horizon at night)
    Vec3 sunColor{1.0f};
    Vec3 moonDirection{0.0f, -1.0f, 0.0f};
    f32 moon = 0.0f;  ///< visibility of the moon disc 0 .. 1
    f32 stars = 0.0f; ///< 0 .. 1
};

/// Point light (torch, campfire). Flicker etc. is game/world logic that changes these per frame.
struct PointLight
{
    Vec3 position{0.0f};
    f32 radius = 5.0f; ///< Influence ends exactly here.
    Vec3 color{1.0f};
    f32 intensity = 1.0f;
};

/// Fog amount 0..1 at `distance`: 1 - exp(-((distance - start) * density)²), 0 before `start`.
/// Same formula as common/fog.glsl.
[[nodiscard]] f32 fogFactor(f32 distance, f32 start, f32 density) noexcept;
/// Density at which the fog reaches `amount` (e.g. 0.9) at `distance` – for configuring by distance.
[[nodiscard]] f32 fogDensityFor(f32 amount, f32 distance, f32 start) noexcept;

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
    std::array<Mat4, 4> cascadeViewProjection;
    Vec4 cascadeSplits;               ///< view depth where cascade i ends
    std::array<Vec4, 4> cascadeRects; ///< atlas tile per cascade
    Vec4 cascadeTexelSize;            ///< world size of a shadow texel per cascade
    Vec4 shadowParams;                ///< x enabled, y cascades, z distance, w normal offset (texels)
    Vec4 shadowExtra;                 ///< x debug colours, y atlas texel size (uv)
    Vec4 cameraPosition;
    Vec4 cameraForward;
    Vec4 fogColorStart; ///< rgb linear, w start
    Vec4 fogParams;     ///< x density
    std::array<Vec4, LightList::kMaxPerFrame> pointPositionRadius;
    std::array<Vec4, LightList::kMaxPerFrame> pointColor; ///< rgb * intensity
};
static_assert(sizeof(GpuLighting) ==
                  5 * 16 + 4 * 64 + 16 + 4 * 16 + 7 * 16 + 2 * 16 * LightList::kMaxPerFrame,
              "must match std140 (common/lighting.glsl)");

[[nodiscard]] GpuLighting packLighting(const Environment& environment, const LightList& lights) noexcept;

struct Cascade;
struct ShadowSettings;
struct Camera;
/// Adds the sun's shadow cascades (and the camera they were computed for) to packed lighting.
void packShadows(GpuLighting& gpu, std::span<const Cascade> cascades, const ShadowSettings& settings,
                 const Camera& camera, bool debugColours) noexcept;
} // namespace g7::render

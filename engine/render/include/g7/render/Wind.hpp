#pragma once

// Wind (M17 weather part W, docs/design/wetter.md, contract in coordination.md "Windgewicht"): one global
// wind per frame (direction, strength and gusts from the weather) bends the vegetation and swings the guild
// signs in the vertex shader (common/wind.glsl). Which materials move, and how, says the glTF material
// (`extras.wind`, asset::WindKind); the weight per vertex comes from its height in the model. The functions
// here are the shader's formulas on the CPU (tests, particles).

#include <g7/asset/MeshData.hpp>
#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>

namespace g7::render
{
/// The wind of a frame.
struct WindFrame
{
    Vec2 direction{1.0f, 0.0f}; ///< where it blows to, unit vector in world xz
    f32 strength = 0.0f;        ///< 0 still .. 1 storm (world::WeatherSample::wind; 0 with `render.wind` off)
    f32 gusts = 0.0f;           ///< 0 steady .. 1 very gusty
    f32 time = 0.0f;            ///< seconds (the engine wraps it)
};

/// How one material moves: its asset::WindKind and the model's height range (the weight's 0 and 1).
struct WindBend
{
    asset::WindKind kind = asset::WindKind::None;
    f32 strength = 1.0f; ///< extras.wind_strength
    f32 foot = 0.0f;     ///< model-space y of weight 0 (the model's lowest point)
    f32 height = 1.0f;   ///< model-space height to weight 1 (the model's top)
    Vec3 pivot{0.0f};    ///< Hanging: swings about this point (model space)
    /// Hanging: the hinge - 0 swings freely (a lantern on one chain), 1 about the model's X axis, 2 about its
    /// Z axis (a sign on its arm: only the wind across the board moves it).
    u8 hinge = 0;
};

/// Gust cells about 30 m across, drifting downwind at 6 m/s.
inline constexpr f32 kWindGustScale = 0.035f;
inline constexpr f32 kWindGustSpeed = 6.0f;
/// Displacement at full wind (contract): tree tops about 4 % of the height, grass about 25 %, cloth 15 %,
/// signs swing up to 30 degrees.
inline constexpr f32 kWindTreeBend = 0.04f;
inline constexpr f32 kWindGrassBend = 0.25f;
inline constexpr f32 kWindClothBend = 0.15f;
inline constexpr f32 kWindHangingDegrees = 30.0f;
/// Particles drift with up to this speed at full wind (times their emitter's `wind` factor).
inline constexpr f32 kWindParticleSpeed = 4.0f;

/// The bend of a material in a model with these bounds (model space). Hanging models clearly longer in one
/// horizontal direction (a sign with its arm) hinge about that axis; others swing freely.
[[nodiscard]] WindBend windBend(const asset::MaterialInfo& material, const AABB& modelBounds) noexcept;

/// Smooth value noise 0..1 (same hash as common/wind.glsl).
[[nodiscard]] f32 windNoise(Vec2 p) noexcept;
/// The wind's strength at a place, gusts included: strength * (1 +- 1.5 * gusts), within 0..1.
[[nodiscard]] f32 windAt(const WindFrame& frame, Vec2 xz) noexcept;
/// Weight 0..1 of a model-space height: quadratic for trees and leaves, linear for grass and cloth.
[[nodiscard]] f32 windWeight(const WindBend& bend, f32 y) noexcept;
/// Where the wind moves a vertex: world position of `modelPosition` with the bend applied (model = the
/// instance's transform). Without wind or for WindKind::None the unbent position.
[[nodiscard]] Vec3 windBent(const WindFrame& frame, const WindBend& bend, const Mat4& model,
                            const Vec3& modelPosition) noexcept;
/// The drift velocity of particles at a place (m/s, world), before their emitter's factor.
[[nodiscard]] Vec3 windVelocity(const WindFrame& frame, const Vec3& position) noexcept;

/// std140 layout of the Wind block (binding 4) in common/wind.glsl.
struct GpuWind
{
    Vec4 state{1.0f, 0.0f, 0.0f, 0.0f}; ///< direction xz, strength, gusts
    Vec4 time{0.0f};                    ///< seconds
};
static_assert(sizeof(GpuWind) == 32);
[[nodiscard]] GpuWind packWind(const WindFrame& frame) noexcept;
} // namespace g7::render

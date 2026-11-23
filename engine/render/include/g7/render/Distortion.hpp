#pragma once

// Spell distortion (M17, plan "Magie-Nachträge" part V, owner decisions 11-13 of 2026-11-08): effects with
// `distort` in data/fx/*.toml write screen offsets into a buffer (heat haze behind the fire bolt and over big
// fires, a shock ring where it strikes, a swirl round the hands while charging); the post pass reads the
// scene at the offset places. A short wobble of the whole picture when the hero changes shape. Subtle:
// offsets of at most about 1 % of the picture's width.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <optional>
#include <string_view>

namespace g7::render
{
class Device;
class SceneTarget;

enum class DistortShape : u8
{
    Haze,  ///< flickering, rises (heat)
    Ring,  ///< grows over the particle's life (shock wave)
    Swirl, ///< turns round the middle
};
[[nodiscard]] std::optional<DistortShape> parseDistortShape(std::string_view text) noexcept;

/// Largest offsets (fractions of the picture) at strength 1; the same numbers as particle_distort.frag.
inline constexpr f32 kDistortHaze = 0.004f;
inline constexpr f32 kDistortRing = 0.01f;
inline constexpr f32 kDistortSwirl = 0.006f;
/// The hero's change of shape (decision 12): the whole picture wobbles this long, by up to this much.
inline constexpr f32 kWobbleSeconds = 0.5f;
inline constexpr f32 kWobbleAmount = 0.006f;

/// Offset (fraction of the picture) of a point `p` (-1..1 over the particle's quad) of a distorting particle,
/// `age` 0..1 over its life. Same as particle_distort.frag (tests).
[[nodiscard]] Vec2 distortOffset(DistortShape shape, Vec2 p, f32 strength, f32 age) noexcept;

/// The offset buffer: RGBA16F (offset in rg) at the scene's size, sharing the scene's depth so effects behind
/// walls write nothing. Cleared each frame it is used.
class DistortionTarget
{
public:
    /// (Re)creates the buffer for `scene` if its size changed; binds it and clears it to no offset.
    [[nodiscard]] Result<void> begin(Device& device, const SceneTarget& scene);
    [[nodiscard]] const rhi::Texture& texture() const noexcept { return m_offsets; }

private:
    rhi::Texture m_offsets;
    rhi::Framebuffer m_framebuffer;
    const rhi::Texture* m_depth = nullptr;
};
} // namespace g7::render

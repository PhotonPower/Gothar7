#pragma once

// Bloom (M17 part P1, render.md "Plan M17"): the bright parts of the HDR scene glow into their surroundings.
// A down chain of half-size levels (13-tap filter; the first applies the threshold with a soft knee) and an
// up chain with a tent filter adding each level into the next larger one; post.frag adds the result before
// the tonemapping.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <vector>

namespace g7::render
{
class Device;
class ShaderLibrary;

struct BloomSettings
{
    bool enabled = true;
    f32 threshold = 1.0f; ///< luminance from which a pixel glows (linear HDR)
    f32 knee = 0.5f;      ///< soft transition below the threshold (0: hard cut)
    f32 intensity = 0.12f;
    u32 levels = 5; ///< down steps (each half the size)
};

/// The part of a colour that glows: brightness above the threshold, softly (Karis' quadratic knee); the
/// colour's hue stays. Same curve as bloom_down.frag (tests).
[[nodiscard]] Vec3 bloomPrefilter(const Vec3& color, f32 threshold, f32 knee) noexcept;

class Bloom
{
public:
    Bloom() = default;
    [[nodiscard]] static Result<Bloom> create(Device& device, ShaderLibrary& shaders);
    /// The level textures for a scene of this size (recreated when it changes).
    [[nodiscard]] Result<void> resize(Device& device, u32 width, u32 height, u32 levels);
    /// Runs the chains on `scene`; afterwards result() holds the glow at half the scene size.
    void render(Device& device, const rhi::Texture& scene, const BloomSettings& settings);
    /// The glow (level 0, half size) after render(); nullptr before or when disabled.
    [[nodiscard]] const rhi::Texture* result() const noexcept;

private:
    rhi::ShaderProgram* m_down = nullptr; // owned by the ShaderLibrary
    rhi::ShaderProgram* m_up = nullptr;
    rhi::Pipeline m_downPipeline;
    rhi::Pipeline m_upPipeline;
    rhi::Sampler m_sampler;
    std::vector<rhi::Texture> m_levels;
    std::vector<rhi::Framebuffer> m_framebuffers;
    u32 m_width = 0;
    u32 m_height = 0;
    bool m_rendered = false;
};
} // namespace g7::render

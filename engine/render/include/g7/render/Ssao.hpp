#pragma once

// Screen-space ambient occlusion (M17 part P3, render.md "Plan M17"): from the depth of a depth prepass, at
// half resolution, 12 samples in a hemisphere around the normal reconstructed from the depth, then a
// depth-aware blur. The forward shaders damp only the ambient light with it (common/lighting.glsl); sun and
// point lights stay.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <vector>

namespace g7::render
{
class Device;
class ShaderLibrary;
struct Camera;

struct SsaoSettings
{
    f32 radius = 0.8f;   ///< metres around a point that can occlude it
    f32 strength = 1.5f; ///< how much the occluded part darkens (above 1: stronger than physical)
};

inline constexpr u32 kSsaoSamples = 12;

/// The sample offsets: in the unit hemisphere around +Z, more of them near the centre (deterministic).
[[nodiscard]] std::vector<Vec3> ssaoKernel(u32 count);

class Ssao
{
public:
    Ssao() = default;
    [[nodiscard]] static Result<Ssao> create(Device& device, ShaderLibrary& shaders);
    /// The targets for a scene of this size (half of it; recreated when it changes).
    [[nodiscard]] Result<void> resize(Device& device, u32 width, u32 height);
    /// From the scene's depth (reverse-Z, not bound to the current framebuffer); afterwards result() holds
    /// the occlusion (R8, 1 = open) at half size. Leaves another framebuffer bound.
    void render(Device& device, const rhi::Texture& depth, const Camera& camera,
                const SsaoSettings& settings);
    [[nodiscard]] const rhi::Texture* result() const noexcept { return m_rendered ? &m_blurred : nullptr; }
    [[nodiscard]] bool ready() const noexcept { return m_ao != nullptr; }

private:
    rhi::ShaderProgram* m_ao = nullptr; // owned by the ShaderLibrary
    rhi::ShaderProgram* m_blur = nullptr;
    rhi::Pipeline m_aoPipeline;
    rhi::Pipeline m_blurPipeline;
    rhi::Sampler m_point;
    rhi::Sampler m_linear;
    rhi::Texture m_raw;
    rhi::Texture m_blurred;
    rhi::Framebuffer m_rawTarget;
    rhi::Framebuffer m_blurTarget;
    std::vector<Vec3> m_kernel;
    u32 m_width = 0;
    u32 m_height = 0;
    bool m_rendered = false;
};
} // namespace g7::render

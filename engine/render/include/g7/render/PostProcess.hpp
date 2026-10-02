#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

namespace g7::render
{
class Device;
class ShaderLibrary;

/// HDR render target of the scene: linear RGBA16F colour (values above 1 allowed) + Depth32F.
/// Everything before the post pass renders here; it follows the window size.
class SceneTarget
{
public:
    SceneTarget() = default;
    [[nodiscard]] static Result<SceneTarget> create(Device& device, u32 width, u32 height);
    /// Recreates the textures if the size changed.
    [[nodiscard]] Result<void> resize(Device& device, u32 width, u32 height);

    [[nodiscard]] const rhi::Framebuffer& framebuffer() const noexcept { return m_framebuffer; }
    [[nodiscard]] const rhi::Texture& color() const noexcept { return m_color; }
    [[nodiscard]] u32 width() const noexcept { return m_framebuffer.width(); }
    [[nodiscard]] u32 height() const noexcept { return m_framebuffer.height(); }

private:
    rhi::Texture m_color;
    rhi::Texture m_depth;
    rhi::Framebuffer m_framebuffer;
};

enum class Tonemapper : u8
{
    Aces,     ///< Filmic (Narkowicz ACES fit): contrast, highlights roll off softly.
    Reinhard, ///< x / (1 + x): neutral, less contrast.
    None,     ///< Clamp at 1 (for comparison).
};

struct PostSettings
{
    Tonemapper tonemapper = Tonemapper::Aces;
    f32 exposure = 1.0f;
};

/// Same curves as post.frag (per channel, after exposure), for tests and tools.
[[nodiscard]] Vec3 tonemap(const Vec3& linear, Tonemapper tonemapper) noexcept;
[[nodiscard]] Tonemapper tonemapperFromName(std::string_view name, Tonemapper fallback) noexcept;

/// Final pass into the window: exposure, tonemapping, sRGB encoding of the HDR scene.
class PostProcess
{
public:
    PostProcess() = default;
    [[nodiscard]] static Result<PostProcess> create(Device& device, ShaderLibrary& shaders);

    /// Draws `scene` into the currently bound target (normally the window) at the given size.
    void apply(Device& device, const SceneTarget& scene, u32 width, u32 height, const PostSettings& settings);

private:
    rhi::ShaderProgram* m_program = nullptr; // owned by the ShaderLibrary
    rhi::Pipeline m_pipeline;
    rhi::Sampler m_sampler;
};
} // namespace g7::render

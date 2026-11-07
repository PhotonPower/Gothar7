#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Grading.hpp>
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
    [[nodiscard]] const rhi::Texture& depth() const noexcept { return m_depth; }
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

/// The view under water (M17 W3): every pixel tinted and fogged by its distance through the water
/// (underwaterColor), the picture slightly wavering.
struct Underwater
{
    Vec3 absorption{1.0f}; ///< per metre (render::WaterStyle)
    Vec3 fogColor{0.05f};  ///< the water's own colour, lit
    f32 distortion = 1.0f; ///< 0: still picture ([render] underwater_distortion = false)
    f32 time = 0.0f;       ///< seconds, moves the wavering
    Mat4 inverseProjection{1.0f};
};

/// The colour of a pixel `metres` away seen through water. Same as post.frag.
[[nodiscard]] Vec3 underwaterColor(const Vec3& color, const Underwater& water, f32 metres) noexcept;

struct PostSettings
{
    Tonemapper tonemapper = Tonemapper::Aces;
    f32 exposure = 1.0f;
    const rhi::Texture* bloom = nullptr; ///< the glow of render::Bloom (M17), nullptr: none
    f32 bloomIntensity = 0.0f;
    std::optional<Grading> grading;       ///< M17 P2; nullopt: none (the image as before)
    std::optional<Underwater> underwater; ///< M17 W3: the camera under water; nullopt: above
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
    rhi::Sampler m_bloomSampler;
};
} // namespace g7::render

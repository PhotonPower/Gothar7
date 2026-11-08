#include <g7/core/Profiler.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Distortion.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <algorithm>

namespace g7::render
{
Vec3 underwaterColor(const Vec3& color, const Underwater& water, f32 metres) noexcept
{
    const Vec3 t = glm::exp(-water.absorption * std::max(metres, 0.0f));
    return color * t + water.fogColor * (Vec3(1.0f) - t);
}

Result<SceneTarget> SceneTarget::create(Device& device, u32 width, u32 height)
{
    SceneTarget target;
    if (auto result = target.resize(device, width, height); !result)
    {
        return result.error();
    }
    return target;
}

Result<void> SceneTarget::resize(Device& device, u32 width, u32 height)
{
    width = std::max(width, 1u); // minimised windows report 0
    height = std::max(height, 1u);
    if (width == m_framebuffer.width() && height == m_framebuffer.height())
    {
        return {};
    }
    auto color = device.createTexture({width, height, rhi::Format::RGBA16F, 1});
    auto depth = device.createTexture({width, height, rhi::Format::Depth32F, 1});
    if (!color || !depth)
    {
        return Error{"cannot create the HDR scene target"};
    }
    rhi::Texture newColor = std::move(color).value();
    rhi::Texture newDepth = std::move(depth).value();
    auto framebuffer = device.createFramebuffer({{&newColor}, &newDepth});
    if (!framebuffer)
    {
        return framebuffer.error();
    }
    m_framebuffer = std::move(framebuffer).value();
    m_color = std::move(newColor);
    m_depth = std::move(newDepth);
    return {};
}

Vec3 tonemap(const Vec3& linear, Tonemapper tonemapper) noexcept
{
    const Vec3 x = glm::max(linear, Vec3(0.0f));
    switch (tonemapper)
    {
    case Tonemapper::Aces:
        return glm::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
    case Tonemapper::Reinhard:
        return x / (Vec3(1.0f) + x);
    default:
        return glm::min(x, Vec3(1.0f));
    }
}

Tonemapper tonemapperFromName(std::string_view name, Tonemapper fallback) noexcept
{
    if (equalsIgnoreCase(name, "aces"))
    {
        return Tonemapper::Aces;
    }
    if (equalsIgnoreCase(name, "reinhard"))
    {
        return Tonemapper::Reinhard;
    }
    if (equalsIgnoreCase(name, "none"))
    {
        return Tonemapper::None;
    }
    return fallback;
}

Result<PostProcess> PostProcess::create(Device& device, ShaderLibrary& shaders)
{
    PostProcess post;
    auto program = shaders.load("post", {"post.vert", "post.frag", {}});
    if (!program)
    {
        return program.error();
    }
    post.m_program = program.value();
    rhi::PipelineDesc desc;
    desc.program = post.m_program;
    desc.cull = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    auto pipeline = device.createPipeline(desc);
    if (!pipeline)
    {
        return pipeline.error();
    }
    post.m_pipeline = std::move(pipeline).value();
    rhi::SamplerDesc samplerDesc;
    samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter =
        rhi::Filter::Nearest; // 1:1 pixels
    samplerDesc.wrapU = samplerDesc.wrapV = rhi::Wrap::Clamp;
    auto sampler = device.createSampler(samplerDesc);
    if (!sampler)
    {
        return sampler.error();
    }
    post.m_sampler = std::move(sampler).value();
    rhi::SamplerDesc bloomDesc; // the half-size glow, smoothly up to the window size
    bloomDesc.minFilter = bloomDesc.magFilter = rhi::Filter::Linear;
    bloomDesc.mipFilter = rhi::Filter::Nearest;
    bloomDesc.wrapU = bloomDesc.wrapV = rhi::Wrap::Clamp;
    auto bloomSampler = device.createSampler(bloomDesc);
    if (!bloomSampler)
    {
        return bloomSampler.error();
    }
    post.m_bloomSampler = std::move(bloomSampler).value();
    return post;
}

void PostProcess::apply(Device& device, const SceneTarget& scene, u32 width, u32 height,
                        const PostSettings& settings)
{
    G7_PROFILE_SCOPE("render::post");
    device.setViewport(0, 0, width, height);
    m_program->setUniform("uExposure", settings.exposure);
    m_program->setUniform("uTonemapper", static_cast<i32>(settings.tonemapper));
    const bool bloom = settings.bloom != nullptr && settings.bloomIntensity > 0.0f;
    m_program->setUniform("uBloomIntensity", bloom ? settings.bloomIntensity : 0.0f);
    m_program->setUniform("uGrade", settings.grading ? 1 : 0);
    m_program->setUniform("uUnderwater", settings.underwater ? 1 : 0);
    m_program->setUniform("uDistort", settings.distortion != nullptr ? 1 : 0); // M17 V
    m_program->setUniform("uWobble", settings.wobble * kWobbleAmount);
    m_program->setUniform("uWobbleTime", settings.wobbleTime);
    if (settings.underwater)
    {
        const Underwater& w = *settings.underwater;
        m_program->setUniform("uWaterAbsorption", w.absorption);
        m_program->setUniform("uWaterFog", w.fogColor);
        m_program->setUniform("uWaterDistortion", w.distortion);
        m_program->setUniform("uWaterTime", w.time);
        m_program->setUniform("uInverseProjection", w.inverseProjection);
    }
    if (settings.grading)
    {
        const Grading& g = *settings.grading;
        m_program->setUniform("uTemperature", g.temperature);
        m_program->setUniform("uTint", g.tint);
        m_program->setUniform("uSaturation", g.saturation);
        m_program->setUniform("uContrast", g.contrast);
        m_program->setUniform("uLift", g.lift);
        m_program->setUniform("uGamma", g.gamma);
        m_program->setUniform("uGain", g.gain);
        m_program->setUniform("uVignette", g.vignette);
    }
    device.bindPipeline(m_pipeline);
    device.bindTexture(0, scene.color(), m_sampler);
    if (bloom)
    {
        device.bindTexture(1, *settings.bloom, m_bloomSampler);
    }
    if (settings.underwater)
    {
        device.bindTexture(2, scene.depth(), m_sampler);
    }
    if (settings.distortion != nullptr)
    {
        device.bindTexture(3, *settings.distortion, m_sampler);
    }
    device.draw(3); // fullscreen triangle
}
} // namespace g7::render

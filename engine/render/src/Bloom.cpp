#include <g7/render/Bloom.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <algorithm>

namespace g7::render
{
Vec3 bloomPrefilter(const Vec3& color, f32 threshold, f32 knee) noexcept
{
    const f32 brightness = std::max(color.r, std::max(color.g, color.b));
    const f32 k = std::max(threshold * knee, 1e-5f);
    // Quadratic soft knee between threshold - k and threshold + k, linear above.
    f32 soft = std::clamp(brightness - threshold + k, 0.0f, 2.0f * k);
    soft = soft * soft / (4.0f * k);
    const f32 contribution = std::max(soft, brightness - threshold) / std::max(brightness, 1e-5f);
    return color * std::max(contribution, 0.0f);
}

Result<Bloom> Bloom::create(Device& device, ShaderLibrary& shaders)
{
    Bloom bloom;
    auto down = shaders.load("bloom_down", {"post.vert", "bloom_down.frag", {}});
    auto up = shaders.load("bloom_up", {"post.vert", "bloom_up.frag", {}});
    if (!down)
    {
        return down.error();
    }
    if (!up)
    {
        return up.error();
    }
    bloom.m_down = down.value();
    bloom.m_up = up.value();
    rhi::PipelineDesc desc;
    desc.cull = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.program = bloom.m_down;
    auto downPipeline = device.createPipeline(desc);
    desc.program = bloom.m_up;
    desc.blend = rhi::BlendMode::Additive; // each level adds onto the next larger one
    auto upPipeline = device.createPipeline(desc);
    if (!downPipeline || !upPipeline)
    {
        return Error{"bloom: cannot create the pipelines"};
    }
    bloom.m_downPipeline = std::move(downPipeline).value();
    bloom.m_upPipeline = std::move(upPipeline).value();
    rhi::SamplerDesc samplerDesc;
    samplerDesc.minFilter = samplerDesc.magFilter = rhi::Filter::Linear;
    samplerDesc.mipFilter = rhi::Filter::Nearest;
    samplerDesc.wrapU = samplerDesc.wrapV = rhi::Wrap::Clamp;
    auto sampler = device.createSampler(samplerDesc);
    if (!sampler)
    {
        return sampler.error();
    }
    bloom.m_sampler = std::move(sampler).value();
    return bloom;
}

Result<void> Bloom::resize(Device& device, u32 width, u32 height, u32 levels)
{
    levels = std::clamp(levels, 1u, 8u);
    if (width == m_width && height == m_height && levels == m_levels.size())
    {
        return {};
    }
    m_levels.clear();
    m_framebuffers.clear();
    m_rendered = false;
    u32 w = width;
    u32 h = height;
    for (u32 i = 0; i < levels; ++i)
    {
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
        auto texture = device.createTexture({w, h, rhi::Format::RGBA16F, 1});
        if (!texture)
        {
            return texture.error();
        }
        m_levels.push_back(std::move(texture).value());
    }
    for (const rhi::Texture& level : m_levels)
    {
        auto framebuffer = device.createFramebuffer({{&level}, nullptr});
        if (!framebuffer)
        {
            return framebuffer.error();
        }
        m_framebuffers.push_back(std::move(framebuffer).value());
    }
    m_width = width;
    m_height = height;
    return {};
}

void Bloom::render(Device& device, const rhi::Texture& scene, const BloomSettings& settings)
{
    m_rendered = false;
    if (!settings.enabled || settings.intensity <= 0.0f || m_levels.empty())
    {
        return;
    }
    // Down: the scene (with the threshold) into level 0, each level into the next smaller one.
    device.bindPipeline(m_downPipeline);
    for (usize i = 0; i < m_levels.size(); ++i)
    {
        const rhi::Texture& source = i == 0 ? scene : m_levels[i - 1];
        const rhi::TextureDesc& target = m_levels[i].desc();
        device.bindFramebuffer(&m_framebuffers[i]);
        device.setViewport(0, 0, target.width, target.height);
        m_down->setUniform("uTexel", Vec2(1.0f / static_cast<f32>(source.desc().width),
                                          1.0f / static_cast<f32>(source.desc().height)));
        m_down->setUniform("uPrefilter", i == 0 ? 1 : 0);
        m_down->setUniform("uThreshold", settings.threshold);
        m_down->setUniform("uKnee", settings.knee);
        device.bindTexture(0, source, m_sampler);
        device.draw(3);
    }
    // Up: from the smallest level, a tent-filtered copy added onto each larger level.
    device.bindPipeline(m_upPipeline);
    for (usize i = m_levels.size() - 1; i > 0; --i)
    {
        const rhi::Texture& source = m_levels[i];
        const rhi::TextureDesc& target = m_levels[i - 1].desc();
        device.bindFramebuffer(&m_framebuffers[i - 1]);
        device.setViewport(0, 0, target.width, target.height);
        m_up->setUniform("uTexel", Vec2(1.0f / static_cast<f32>(source.desc().width),
                                        1.0f / static_cast<f32>(source.desc().height)));
        device.bindTexture(0, source, m_sampler);
        device.draw(3);
    }
    m_rendered = true;
}

const rhi::Texture* Bloom::result() const noexcept
{
    return m_rendered && !m_levels.empty() ? &m_levels.front() : nullptr;
}
} // namespace g7::render

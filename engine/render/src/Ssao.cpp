#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Ssao.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace g7::render
{
std::vector<Vec3> ssaoKernel(u32 count)
{
    std::vector<Vec3> kernel;
    kernel.reserve(count);
    for (u32 i = 0; i < count; ++i)
    {
        // A spiral over the hemisphere (golden angle), heights evenly spread, lengths growing quadratically.
        const f32 t = (static_cast<f32>(i) + 0.5f) / static_cast<f32>(count);
        const f32 z = 0.15f + 0.85f * (1.0f - t);
        const f32 r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        const f32 angle = static_cast<f32>(i) * 2.39996323f;
        const f32 scale = 0.1f + 0.9f * t * t;
        kernel.emplace_back(Vec3(r * std::cos(angle), r * std::sin(angle), z) * scale);
    }
    return kernel;
}

Result<Ssao> Ssao::create(Device& device, ShaderLibrary& shaders)
{
    Ssao ssao;
    auto ao = shaders.load("ssao", {"post.vert", "ssao.frag", {}});
    if (!ao)
    {
        return ao.error();
    }
    auto blur = shaders.load("ssao_blur", {"post.vert", "ssao_blur.frag", {}});
    if (!blur)
    {
        return blur.error();
    }
    ssao.m_ao = ao.value();
    ssao.m_blur = blur.value();
    rhi::PipelineDesc desc;
    desc.cull = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.program = ssao.m_ao;
    auto aoPipeline = device.createPipeline(desc);
    desc.program = ssao.m_blur;
    auto blurPipeline = device.createPipeline(desc);
    if (!aoPipeline || !blurPipeline)
    {
        return Error{"ssao: cannot create the pipelines"};
    }
    ssao.m_aoPipeline = std::move(aoPipeline).value();
    ssao.m_blurPipeline = std::move(blurPipeline).value();
    rhi::SamplerDesc point;
    point.minFilter = point.magFilter = point.mipFilter = rhi::Filter::Nearest;
    point.wrapU = point.wrapV = rhi::Wrap::Clamp;
    rhi::SamplerDesc linear = point;
    linear.minFilter = linear.magFilter = rhi::Filter::Linear;
    auto pointSampler = device.createSampler(point);
    auto linearSampler = device.createSampler(linear);
    if (!pointSampler || !linearSampler)
    {
        return Error{"ssao: cannot create the samplers"};
    }
    ssao.m_point = std::move(pointSampler).value();
    ssao.m_linear = std::move(linearSampler).value();
    ssao.m_kernel = ssaoKernel(kSsaoSamples);
    return ssao;
}

Result<void> Ssao::resize(Device& device, u32 width, u32 height)
{
    if (width == m_width && height == m_height)
    {
        return {};
    }
    const u32 w = std::max(width / 2, 1u);
    const u32 h = std::max(height / 2, 1u);
    auto raw = device.createTexture({w, h, rhi::Format::R8, 1});
    auto blurred = device.createTexture({w, h, rhi::Format::R8, 1});
    if (!raw || !blurred)
    {
        return Error{"ssao: cannot create the targets"};
    }
    m_raw = std::move(raw).value();
    m_blurred = std::move(blurred).value();
    auto rawTarget = device.createFramebuffer({{&m_raw}, nullptr});
    auto blurTarget = device.createFramebuffer({{&m_blurred}, nullptr});
    if (!rawTarget || !blurTarget)
    {
        return Error{"ssao: cannot create the framebuffers"};
    }
    m_rawTarget = std::move(rawTarget).value();
    m_blurTarget = std::move(blurTarget).value();
    m_width = width;
    m_height = height;
    m_rendered = false;
    return {};
}

void Ssao::render(Device& device, const rhi::Texture& depth, const Camera& camera,
                  const SsaoSettings& settings)
{
    m_rendered = false;
    if (m_ao == nullptr || m_width == 0)
    {
        return;
    }
    const u32 w = m_raw.desc().width;
    const u32 h = m_raw.desc().height;
    const Mat4 projection = camera.projection();
    device.bindFramebuffer(&m_rawTarget);
    device.setViewport(0, 0, w, h);
    device.bindPipeline(m_aoPipeline);
    m_ao->setUniform("uProjection", projection);
    m_ao->setUniform("uInverseProjection", glm::inverse(projection));
    m_ao->setUniform("uTexel", Vec2(1.0f / static_cast<f32>(m_width), 1.0f / static_cast<f32>(m_height)));
    m_ao->setUniform("uRadius", settings.radius);
    m_ao->setUniform("uStrength", settings.strength);
    m_ao->setUniform("uKernel", std::span<const f32>(&m_kernel[0].x, m_kernel.size() * 3));
    device.bindTexture(0, depth, m_point);
    device.draw(3);
    device.bindFramebuffer(&m_blurTarget);
    device.setViewport(0, 0, w, h);
    device.bindPipeline(m_blurPipeline);
    m_blur->setUniform("uInverseProjection", glm::inverse(projection));
    m_blur->setUniform("uTexel", Vec2(1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h)));
    device.bindTexture(0, m_raw, m_point);
    device.bindTexture(1, depth, m_point);
    device.draw(3);
    m_rendered = true;
}
} // namespace g7::render

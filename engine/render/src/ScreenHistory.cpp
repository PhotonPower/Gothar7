#include <g7/core/Profiler.hpp>
#include <g7/render/ScreenHistory.hpp>

#include <utility>

namespace g7::render
{
Result<ScreenHistory> ScreenHistory::create(Device& device, ShaderLibrary& shaders)
{
    ScreenHistory history;
    // The water's copy shader: colour and depth of the scene into two attachments.
    auto copy = shaders.load("water_copy", {"post.vert", "water_copy.frag", {}});
    if (!copy)
    {
        return copy.error();
    }
    history.m_copy = copy.value();
    rhi::PipelineDesc desc;
    desc.program = history.m_copy;
    desc.cull = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    auto pipeline = device.createPipeline(desc);
    rhi::SamplerDesc point;
    point.minFilter = point.magFilter = rhi::Filter::Nearest;
    point.mipFilter = rhi::Filter::Nearest;
    point.wrapU = point.wrapV = rhi::Wrap::Clamp;
    auto sampler = device.createSampler(point);
    if (!pipeline || !sampler)
    {
        return Error{"screen history: cannot create the copy pipeline"};
    }
    history.m_pipeline = std::move(pipeline).value();
    history.m_pointSampler = std::move(sampler).value();
    return history;
}

Result<void> ScreenHistory::capture(Device& device, const rhi::Texture& color, const rhi::Texture& depth,
                                    const Camera& camera)
{
    G7_PROFILE_SCOPE("render::screen history");
    const u32 width = color.desc().width;
    const u32 height = color.desc().height;
    if (width != m_width || height != m_height)
    {
        m_valid = false;
        auto c = device.createTexture({width, height, rhi::Format::RGBA16F, 1});
        auto d = device.createTexture({width, height, rhi::Format::R32F, 1});
        if (!c || !d)
        {
            return Error{"screen history: cannot create the copies"};
        }
        m_color = std::move(c).value();
        m_depth = std::move(d).value();
        auto framebuffer = device.createFramebuffer({{&m_color, &m_depth}, nullptr});
        if (!framebuffer)
        {
            return framebuffer.error();
        }
        m_framebuffer = std::move(framebuffer).value();
        m_width = width;
        m_height = height;
    }
    device.bindFramebuffer(&m_framebuffer);
    device.setViewport(0, 0, width, height);
    device.bindPipeline(m_pipeline);
    device.bindTexture(0, color, m_pointSampler);
    device.bindTexture(1, depth, m_pointSampler);
    device.draw(3);
    m_camera = camera;
    m_valid = true;
    return {};
}
} // namespace g7::render

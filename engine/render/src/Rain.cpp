#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Rain.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace g7::render
{
namespace
{
constexpr f32 kStaleDistance = 8.0f;
constexpr u32 kSplashes = 600; ///< splash quads at full rain (medium and high)
} // namespace

std::optional<RainQuality> parseRainQuality(std::string_view text) noexcept
{
    if (text == "low")
    {
        return RainQuality::Low;
    }
    if (text == "medium")
    {
        return RainQuality::Medium;
    }
    if (text == "high")
    {
        return RainQuality::High;
    }
    return std::nullopt;
}

u32 rainStreaks(RainQuality quality) noexcept
{
    switch (quality)
    {
    case RainQuality::Low:
        return 4000;
    case RainQuality::Medium:
        return 12000;
    case RainQuality::High:
        return 24000;
    }
    return 12000;
}

Mat4 rainMapViewProjection(const Vec3& centre) noexcept
{
    // Looking straight down from kRainMapHeight / 2 above the centre; x to the right, -z up on the map.
    const Vec3 eye = centre + Vec3(0.0f, kRainMapHeight * 0.5f, 0.0f);
    const Mat4 view = glm::lookAt(eye, centre, Vec3(0.0f, 0.0f, -1.0f));
    Mat4 projection(0.0f); // orthographic, depth 0 at the eye .. 1 at kRainMapHeight below (0..1 clip range)
    projection[0][0] = 1.0f / kRainMapRadius;
    projection[1][1] = 1.0f / kRainMapRadius;
    projection[2][2] = -1.0f / kRainMapHeight;
    projection[3][3] = 1.0f;
    return projection * view;
}

Result<RainRenderer> RainRenderer::create(Device& device, ShaderLibrary& shaders, u32 mapSize)
{
    RainRenderer rain;
    auto program = shaders.load("rain", {"rain.vert", "rain.frag", {}});
    if (!program)
    {
        return program.error();
    }
    rain.m_program = program.value();
    rhi::PipelineDesc desc;
    desc.program = rain.m_program;
    desc.cull = rhi::CullMode::None;
    desc.depthTest = true; // behind the scene's surfaces
    desc.depthWrite = false;
    desc.blend = rhi::BlendMode::Additive;
    auto pipeline = device.createPipeline(desc);
    if (!pipeline)
    {
        return pipeline.error();
    }
    rain.m_pipeline = std::move(pipeline).value();
    auto map = device.createTexture({mapSize, mapSize, rhi::Format::Depth32F, 1});
    if (!map)
    {
        return map.error();
    }
    rain.m_map = std::move(map).value();
    auto target = device.createFramebuffer({{}, &rain.m_map});
    if (!target)
    {
        return target.error();
    }
    rain.m_mapTarget = std::move(target).value();
    rhi::SamplerDesc point;
    point.minFilter = point.magFilter = point.mipFilter = rhi::Filter::Nearest;
    point.wrapU = point.wrapV = rhi::Wrap::Clamp;
    auto sampler = device.createSampler(point);
    if (!sampler)
    {
        return sampler.error();
    }
    rain.m_mapSampler = std::move(sampler).value();
    return rain;
}

bool RainRenderer::mapStale(const Vec3& eye) const noexcept
{
    return !m_mapValid || glm::length(Vec2(eye.x - m_mapCentre.x, eye.z - m_mapCentre.z)) > kStaleDistance ||
           std::abs(eye.y - m_mapCentre.y) > kStaleDistance;
}

void RainRenderer::beginMap(Device& device, const Vec3& eye)
{
    m_mapCentre = Vec3(std::round(eye.x / 4.0f) * 4.0f, std::round(eye.y / 4.0f) * 4.0f,
                       std::round(eye.z / 4.0f) * 4.0f);
    m_mapViewProjection = rainMapViewProjection(m_mapCentre);
    device.bindFramebuffer(&m_mapTarget);
    device.setViewport(0, 0, m_map.desc().width, m_map.desc().height);
    device.clear(std::nullopt, 1.0f); // not reversed: 1 = nothing up to the ground far below
    m_mapValid = false;
}

u32 RainRenderer::draw(Device& device, const Camera& camera, const RainFrame& frame)
{
    if (m_program == nullptr || frame.rain <= 0.0f)
    {
        return 0;
    }
    const u32 streaks =
        static_cast<u32>(static_cast<f32>(rainStreaks(frame.quality)) * std::min(frame.rain, 1.0f));
    const u32 splashes = frame.quality == RainQuality::Low
                             ? 0u
                             : static_cast<u32>(static_cast<f32>(kSplashes) * std::min(frame.rain, 1.0f));
    if (streaks == 0)
    {
        return 0;
    }
    rhi::ShaderProgram& p = *m_program;
    device.bindPipeline(m_pipeline);
    p.setUniform("uViewProjection", camera.viewProjection());
    p.setUniform("uCameraPosition", camera.transform.position);
    p.setUniform("uMapViewProjection", m_mapViewProjection);
    p.setUniform("uHasMap", m_mapValid ? 1 : 0);
    p.setUniform("uMapEyeY", m_mapCentre.y + kRainMapHeight * 0.5f);
    p.setUniform("uMapHeight", kRainMapHeight);
    p.setUniform("uTime", frame.time);
    p.setUniform("uWind", frame.wind);
    p.setUniform("uLight", frame.light);
    p.setUniform("uRain", frame.rain);
    device.bindTexture(0, m_map, m_mapSampler);
    p.setUniform("uSplash", 0);
    device.drawInstanced(6, streaks);
    if (splashes > 0 && m_mapValid)
    {
        p.setUniform("uSplash", 1);
        device.drawInstanced(6, splashes);
    }
    return streaks;
}
} // namespace g7::render

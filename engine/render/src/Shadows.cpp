#include <g7/core/Transform.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Shadows.hpp>

#include <algorithm>
#include <cmath>

namespace g7::render
{
namespace
{
/// Orthographic projection for a light view looking along -Z: xy -r..r -> -1..1, depth 0 at the
/// light-side near plane (z = 0) to 1 at z = -far (0..1 clip range, not reversed).
Mat4 orthographicZeroToOne(f32 radius, f32 far)
{
    Mat4 m(0.0f);
    m[0][0] = 1.0f / radius;
    m[1][1] = 1.0f / radius;
    m[2][2] = -1.0f / far;
    m[3][3] = 1.0f;
    return m;
}
} // namespace

std::vector<f32> cascadeSplits(f32 nearPlane, f32 distance, u32 cascades, f32 lambda)
{
    std::vector<f32> splits(cascades + 1);
    splits[0] = nearPlane;
    for (u32 i = 1; i <= cascades; ++i)
    {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(cascades);
        const f32 logarithmic = nearPlane * std::pow(distance / nearPlane, t);
        const f32 uniform = nearPlane + (distance - nearPlane) * t;
        splits[i] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }
    splits[cascades] = distance; // exact end despite rounding
    return splits;
}

std::vector<Cascade> computeCascades(const Camera& camera, const Vec3& sunDirection,
                                     const ShadowSettings& settings)
{
    const u32 count = std::clamp(settings.cascades, 1u, 4u);
    const f32 distance = std::min(settings.distance, camera.farPlane);
    const auto splits = cascadeSplits(camera.nearPlane, distance, count, settings.splitLambda);

    const Vec3 forward = camera.transform.forward();
    const f32 tanY = std::tan(camera.fovY * 0.5f);
    const f32 tanX = tanY * camera.aspect;
    const Vec3 toSun = glm::normalize(sunDirection);

    std::vector<Cascade> cascades(count);
    for (u32 i = 0; i < count; ++i)
    {
        const f32 n = splits[i];
        const f32 f = splits[i + 1];
        // Bounding sphere of the slice, centred on the view axis: depends only on the slice, not on
        // the camera's rotation. Centre depth c minimises the radius to the near and far corners.
        const f32 k = tanX * tanX + tanY * tanY; // squared corner offset per unit depth
        const f32 c = std::min(f, 0.5f * (n + f) * (1.0f + k));
        const f32 nearCorner = std::sqrt((n - c) * (n - c) + n * n * k);
        const f32 farCorner = std::sqrt((f - c) * (f - c) + f * f * k);
        f32 radius = std::max(nearCorner, farCorner);
        radius = std::ceil(radius * 16.0f) / 16.0f; // avoid float noise changing the size
        const Vec3 center = camera.transform.position + forward * c;

        // Light camera on the sun side of the sphere, extended so casters between the sun and the
        // slice (also behind the player camera) are included.
        const f32 back = radius + settings.casterExtension;
        Transform light;
        light.position = center + toSun * back;
        light.rotation = lookRotation(-toSun, std::abs(toSun.y) > 0.99f ? Vec3(0, 0, 1) : kWorldUp);
        const Mat4 view = light.inverse().toMatrix();
        Mat4 projection = orthographicZeroToOne(radius, back + radius);

        // Snap to whole texels: offset the projection so the world origin lands on a texel corner.
        const f32 halfResolution = static_cast<f32>(settings.resolution) * 0.5f;
        const Vec4 origin = projection * view * Vec4(0, 0, 0, 1);
        const Vec2 texel = Vec2(origin) * halfResolution;
        const Vec2 offset = (glm::round(texel) - texel) / halfResolution;
        projection[3][0] += offset.x;
        projection[3][1] += offset.y;

        Cascade& cascade = cascades[i];
        cascade.viewProjection = projection * view;
        cascade.splitNear = n;
        cascade.splitFar = f;
        cascade.texelWorldSize = 2.0f * radius / static_cast<f32>(settings.resolution);
        cascade.atlasRect = Vec4(static_cast<f32>(i % 2) * 0.5f, static_cast<f32>(i / 2) * 0.5f, 0.5f, 0.5f);
    }
    return cascades;
}

Result<ShadowMap> ShadowMap::create(Device& device, const ShadowSettings& settings)
{
    if (settings.resolution == 0)
    {
        return Error{"shadow map resolution must not be 0"};
    }
    ShadowMap map;
    map.m_settings = settings;
    const u32 size = settings.resolution * 2; // 2x2 cascade tiles
    auto depth = device.createTexture({size, size, rhi::Format::Depth32F, 1});
    if (!depth)
    {
        return depth.error();
    }
    map.m_depth = std::move(depth).value();
    auto framebuffer = device.createFramebuffer({{}, &map.m_depth});
    if (!framebuffer)
    {
        return framebuffer.error();
    }
    map.m_framebuffer = std::move(framebuffer).value();
    rhi::SamplerDesc samplerDesc;
    samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = rhi::Filter::Linear; // 2x2 PCF
    samplerDesc.wrapU = samplerDesc.wrapV = rhi::Wrap::Clamp;
    samplerDesc.compare = rhi::CompareOp::LessEqual;
    auto sampler = device.createSampler(samplerDesc);
    if (!sampler)
    {
        return sampler.error();
    }
    map.m_sampler = std::move(sampler).value();
    return map;
}

void ShadowMap::begin(Device& device)
{
    device.bindFramebuffer(&m_framebuffer);
}

void ShadowMap::beginCascade(Device& device, u32 index) const
{
    // Only this tile is cleared and drawn: the others may keep last frame's depth (Engine redraws far
    // cascades less often).
    const auto r = static_cast<i32>(m_settings.resolution);
    const PixelRect tile{static_cast<i32>(index % 2) * r, static_cast<i32>(index / 2) * r,
                         m_settings.resolution, m_settings.resolution};
    device.setViewport(tile.x, tile.y, tile.width, tile.height);
    device.setScissor(tile); // the clear only; the viewport keeps the draws in the tile
    device.clear(std::nullopt, 1.0f); // shadow depth is not reversed: 1 = far
    device.setScissor(std::nullopt);
}
} // namespace g7::render

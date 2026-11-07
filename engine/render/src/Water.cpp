#include <g7/asset/ImageData.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Geometry.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/TextureUpload.hpp>
#include <g7/render/Water.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace g7::render
{
std::optional<WaterQuality> parseWaterQuality(std::string_view text) noexcept
{
    if (text == "low")
    {
        return WaterQuality::Low;
    }
    if (text == "medium")
    {
        return WaterQuality::Medium;
    }
    if (text == "high")
    {
        return WaterQuality::High;
    }
    return std::nullopt;
}

Result<WaterStyles> WaterStyles::parse(std::string_view toml, std::string_view source)
{
    auto config = Config::parse(toml, source);
    if (!config)
    {
        return config.error();
    }
    const Config& c = config.value();
    const auto error = [&](std::string_view where, std::string_view what)
    { return Error{std::format("{}: {}: {}", source, where, what)}; };
    if (c.get<i64>("version", 0) != 1)
    {
        return error("version", "must be 1");
    }
    WaterStyles styles;
    for (const std::string& name : c.keys("kind"))
    {
        const std::string at = "kind." + name;
        WaterStyle style;
        for (auto [key, target] :
             {std::pair{"absorption", &style.absorption}, std::pair{"scatter", &style.scatter}})
        {
            if (const auto value = c.find<std::vector<f64>>(at + "." + key); value && value->size() == 3)
            {
                *target = Vec3((*value)[0], (*value)[1], (*value)[2]);
            }
            else if (c.contains(at + "." + key))
            {
                return error(at, std::string(key) + " must be [r, g, b]");
            }
        }
        for (auto [key, target] :
             {std::pair{"wave_scale", &style.waveScale}, std::pair{"wave_speed", &style.waveSpeed},
              std::pair{"wave_strength", &style.waveStrength}, std::pair{"flow_speed", &style.flowSpeed},
              std::pair{"refraction", &style.refraction}, std::pair{"glint", &style.glint},
              std::pair{"shininess", &style.shininess}, std::pair{"shore_fade", &style.shoreFade}})
        {
            if (const auto value = c.find<f64>(at + "." + key))
            {
                *target = static_cast<f32>(*value);
            }
            else if (c.contains(at + "." + key))
            {
                return error(at, std::string(key) + " must be a number");
            }
        }
        if (glm::any(glm::lessThan(style.absorption, Vec3(0.0f))))
        {
            return error(at, "absorption must not be negative");
        }
        if (style.waveScale <= 0.0f || style.shininess <= 0.0f || style.shoreFade <= 0.0f)
        {
            return error(at, "wave_scale, shininess and shore_fade must be > 0");
        }
        styles.m_kinds.emplace(name, style);
    }
    styles.m_default = c.get<std::string>("default", "");
    if (!styles.m_default.empty() && !styles.m_kinds.contains(styles.m_default))
    {
        return error("default", "names no [kind." + styles.m_default + "]");
    }
    return styles;
}

const WaterStyle& WaterStyles::style(std::string_view kind) const
{
    if (const auto it = m_kinds.find(kind); it != m_kinds.end())
    {
        return it->second;
    }
    if (const auto it = m_kinds.find(m_default); it != m_kinds.end())
    {
        return it->second;
    }
    return m_fallback;
}

f32 waterFresnel(f32 cosTheta, f32 f0) noexcept
{
    const f32 m = 1.0f - std::clamp(cosTheta, 0.0f, 1.0f);
    return f0 + (1.0f - f0) * m * m * m * m * m;
}

Vec3 waterTransmittance(const Vec3& absorption, f32 metres) noexcept
{
    return glm::exp(-absorption * std::max(metres, 0.0f));
}

std::vector<u8> makeWaterNormals(u32 size)
{
    // A sum of sines with whole frequencies over the tile (so it repeats seamlessly), amplitudes falling with
    // the frequency; the normal from the analytic slope.
    struct Wave
    {
        f32 kx, ky, amplitude, phase;
    };
    std::vector<Wave> waves;
    u32 seed = 0x9e3779b9u;
    const auto next = [&]
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<f32>(seed & 0xffffu) / 65535.0f;
    };
    for (int i = 0; i < 24; ++i)
    {
        f32 kx = std::round(next() * 10.0f - 5.0f);
        f32 ky = std::round(next() * 10.0f - 5.0f);
        if (kx == 0.0f && ky == 0.0f)
        {
            kx = 1.0f;
        }
        const f32 k = std::sqrt(kx * kx + ky * ky);
        waves.push_back({kx, ky, 1.0f / (k * k), next() * 2.0f * std::numbers::pi_v<f32>});
    }
    std::vector<Vec3> slopes(static_cast<usize>(size) * size); // dh/dx, dh/dy, h
    f32 largest = 1e-6f;
    f32 highest = 1e-6f;
    for (u32 y = 0; y < size; ++y)
    {
        for (u32 x = 0; x < size; ++x)
        {
            const f32 u = static_cast<f32>(x) / static_cast<f32>(size);
            const f32 v = static_cast<f32>(y) / static_cast<f32>(size);
            Vec3 s(0.0f);
            for (const Wave& w : waves)
            {
                const f32 angle = 2.0f * std::numbers::pi_v<f32> * (w.kx * u + w.ky * v) + w.phase;
                const f32 c = std::cos(angle) * w.amplitude * 2.0f * std::numbers::pi_v<f32>;
                s += Vec3(c * w.kx, c * w.ky, std::sin(angle) * w.amplitude);
            }
            slopes[y * size + x] = s;
            largest = std::max({largest, std::abs(s.x), std::abs(s.y)});
            highest = std::max(highest, std::abs(s.z));
        }
    }
    std::vector<u8> rgba(slopes.size() * 4);
    const auto byte = [](f32 value)
    { return static_cast<u8>(std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    for (usize i = 0; i < slopes.size(); ++i)
    {
        // The tilt (-slope) scaled to -1..1: the shader multiplies it with wave_strength.
        rgba[i * 4 + 0] = byte(-slopes[i].x / largest);
        rgba[i * 4 + 1] = byte(-slopes[i].y / largest);
        rgba[i * 4 + 2] = byte(slopes[i].z / highest);
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

Result<WaterRenderer> WaterRenderer::create(Device& device, ShaderLibrary& shaders)
{
    WaterRenderer water;
    auto copy = shaders.load("water_copy", {"post.vert", "water_copy.frag", {}});
    if (!copy)
    {
        return copy.error();
    }
    auto surface = shaders.load("water", {"water.vert", "water.frag", {}});
    if (!surface)
    {
        return surface.error();
    }
    water.m_copy = copy.value();
    water.m_water = surface.value();
    rhi::PipelineDesc desc;
    desc.cull = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.program = water.m_copy;
    auto copyPipeline = device.createPipeline(desc);
    desc.program = water.m_water;
    desc.depthTest = true; // behind the opaque scene; writes its depth (particles under it stay hidden)
    desc.depthWrite = true;
    desc.blend = rhi::BlendMode::Alpha; // the shore edge fades out; low quality is see-through
    auto waterPipeline = device.createPipeline(desc);
    if (!copyPipeline || !waterPipeline)
    {
        return Error{"water: cannot create the pipelines"};
    }
    water.m_copyPipeline = std::move(copyPipeline).value();
    water.m_waterPipeline = std::move(waterPipeline).value();
    rhi::SamplerDesc clamp;
    clamp.mipFilter = rhi::Filter::Nearest;
    clamp.wrapU = clamp.wrapV = rhi::Wrap::Clamp;
    rhi::SamplerDesc point = clamp;
    point.minFilter = point.magFilter = rhi::Filter::Nearest;
    rhi::SamplerDesc waves; // repeat, trilinear
    waves.maxAnisotropy = 4.0f;
    auto clampSampler = device.createSampler(clamp);
    auto pointSampler = device.createSampler(point);
    auto waveSampler = device.createSampler(waves);
    if (!clampSampler || !pointSampler || !waveSampler)
    {
        return Error{"water: cannot create the samplers"};
    }
    water.m_clampSampler = std::move(clampSampler).value();
    water.m_pointSampler = std::move(pointSampler).value();
    water.m_waveSampler = std::move(waveSampler).value();
    constexpr u32 kWaveSize = 128;
    asset::ImageData image{kWaveSize, kWaveSize, makeWaterNormals(kWaveSize)};
    auto normals = createTexture(device, image, {false, true, std::nullopt});
    if (!normals)
    {
        return normals.error();
    }
    water.m_normals = std::move(normals).value();
    return water;
}

Result<void> WaterRenderer::resize(Device& device, u32 width, u32 height)
{
    if (width == m_width && height == m_height)
    {
        return {};
    }
    auto color = device.createTexture({width, height, rhi::Format::RGBA16F, 1});
    auto depth = device.createTexture({width, height, rhi::Format::R32F, 1});
    if (!color || !depth)
    {
        return Error{"water: cannot create the copies"};
    }
    m_colorCopy = std::move(color).value();
    m_depthCopy = std::move(depth).value();
    auto framebuffer = device.createFramebuffer({{&m_colorCopy, &m_depthCopy}, nullptr});
    if (!framebuffer)
    {
        return framebuffer.error();
    }
    m_copyFramebuffer = std::move(framebuffer).value();
    m_width = width;
    m_height = height;
    return {};
}

u32 WaterRenderer::draw(Device& device, const SceneTarget& scene, const Camera& camera,
                        std::span<const WaterSurface> surfaces, const WaterFrame& frame)
{
    if (m_water == nullptr)
    {
        return 0;
    }
    const Frustum view = camera.frustum();
    m_visible.clear();
    for (u32 i = 0; i < surfaces.size(); ++i)
    {
        const WaterSurface& s = surfaces[i];
        const f32 c = std::abs(std::cos(s.yaw));
        const f32 n = std::abs(std::sin(s.yaw));
        const Vec3 half(c * s.halfExtents.x + n * s.halfExtents.y, 0.05f,
                        n * s.halfExtents.x + c * s.halfExtents.y);
        if (view.intersects(AABB{s.centre - half, s.centre + half}))
        {
            m_visible.push_back(i);
        }
    }
    if (m_visible.empty())
    {
        return 0;
    }
    const u32 width = scene.width();
    const u32 height = scene.height();
    const bool refract = frame.quality != WaterQuality::Low;
    if (refract)
    {
        if (auto resized = resize(device, width, height); !resized)
        {
            return 0;
        }
        device.bindFramebuffer(&m_copyFramebuffer);
        device.setViewport(0, 0, width, height);
        device.bindPipeline(m_copyPipeline);
        device.bindTexture(0, scene.color(), m_pointSampler);
        device.bindTexture(1, scene.depth(), m_pointSampler);
        device.draw(3);
        device.bindFramebuffer(&scene.framebuffer());
        device.setViewport(0, 0, width, height);
    }
    device.bindPipeline(m_waterPipeline);
    rhi::ShaderProgram& p = *m_water;
    p.setUniform("uViewProjection", camera.viewProjection());
    p.setUniform("uInverseViewProjection", glm::inverse(camera.viewProjection()));
    p.setUniform("uCameraPosition", camera.transform.position);
    p.setUniform("uViewportSize", Vec2(static_cast<f32>(width), static_cast<f32>(height)));
    p.setUniform("uTime", frame.time);
    p.setUniform("uRefract", refract ? 1 : 0);
    p.setUniform("uSunDirection", glm::normalize(frame.sunDirection));
    p.setUniform("uSunColor", frame.sunColor);
    p.setUniform("uAmbient", frame.ambient);
    p.setUniform("uSkyHorizon", frame.skyHorizon);
    p.setUniform("uSkyZenith", frame.skyZenith);
    p.setUniform("uFogColor", frame.fogColor);
    p.setUniform("uFogStart", frame.fogStart);
    p.setUniform("uFogDensity", frame.fogDensity);
    if (refract)
    {
        device.bindTexture(
            0, m_colorCopy,
            m_pointSampler); // point: a filtered sample at the edge of a dry object would mix it in
        device.bindTexture(1, m_depthCopy, m_pointSampler);
    }
    device.bindTexture(2, m_normals, m_waveSampler);
    static const WaterStyle kDefault;
    for (const u32 index : m_visible)
    {
        const WaterSurface& s = surfaces[index];
        const WaterStyle& style = s.style != nullptr ? *s.style : kDefault;
        const Vec2 axis(std::cos(s.yaw), std::sin(s.yaw));
        p.setUniform("uCentre", s.centre);
        p.setUniform("uYaw", axis);
        p.setUniform("uHalfExtents", s.halfExtents);
        // Local +X in world space (rotation about +Y): (cos, 0, -sin).
        p.setUniform("uFlow", Vec2(axis.x, -axis.y) * style.flowSpeed);
        p.setUniform("uAbsorption", style.absorption);
        p.setUniform("uScatter", style.scatter);
        p.setUniform("uWaveScale", style.waveScale);
        p.setUniform("uWaveSpeed", style.waveSpeed);
        p.setUniform("uWaveStrength", style.waveStrength);
        p.setUniform("uRefraction", style.refraction);
        p.setUniform("uGlint", style.glint);
        p.setUniform("uShininess", style.shininess);
        p.setUniform("uShoreFade", style.shoreFade);
        device.draw(6);
    }
    return static_cast<u32>(m_visible.size());
}
} // namespace g7::render

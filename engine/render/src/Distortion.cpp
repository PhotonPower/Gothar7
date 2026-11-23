#include <g7/render/Device.hpp>
#include <g7/render/Distortion.hpp>
#include <g7/render/PostProcess.hpp>

#include <algorithm>
#include <cmath>

namespace g7::render
{
std::optional<DistortShape> parseDistortShape(std::string_view text) noexcept
{
    if (text == "haze")
    {
        return DistortShape::Haze;
    }
    if (text == "ring")
    {
        return DistortShape::Ring;
    }
    if (text == "swirl")
    {
        return DistortShape::Swirl;
    }
    return std::nullopt;
}

Vec2 distortOffset(DistortShape shape, Vec2 p, f32 strength, f32 age) noexcept
{
    const f32 r = glm::length(p);
    if (r >= 1.0f)
    {
        return Vec2(0.0f);
    }
    const f32 falloff = (1.0f - r) * (1.0f - r);
    switch (shape)
    {
    case DistortShape::Haze:
        return Vec2(std::sin(p.y * 7.0f + age * 23.0f), std::cos(p.x * 6.0f - age * 19.0f)) *
               (kDistortHaze * strength * falloff);
    case DistortShape::Ring:
    {
        if (r < 1e-4f)
        {
            return Vec2(0.0f);
        }
        const f32 d = (r - age) / 0.15f;
        return p / r * (kDistortRing * strength * std::exp(-d * d) * (1.0f - age));
    }
    case DistortShape::Swirl:
        if (r < 1e-4f)
        {
            return Vec2(0.0f);
        }
        return Vec2(-p.y, p.x) / r * (kDistortSwirl * strength * falloff);
    }
    return Vec2(0.0f);
}

Result<void> DistortionTarget::begin(Device& device, const SceneTarget& scene)
{
    const u32 w = scene.width();
    const u32 h = scene.height();
    if (m_offsets.desc().width != w || m_offsets.desc().height != h || m_depth != &scene.depth() ||
        m_framebuffer.width() == 0)
    {
        auto offsets = device.createTexture({w, h, rhi::Format::RGBA16F, 1});
        if (!offsets)
        {
            return offsets.error();
        }
        m_offsets = std::move(offsets).value();
        auto framebuffer = device.createFramebuffer({{&m_offsets}, &scene.depth()});
        if (!framebuffer)
        {
            return framebuffer.error();
        }
        m_framebuffer = std::move(framebuffer).value();
        m_depth = &scene.depth();
    }
    device.bindFramebuffer(&m_framebuffer);
    device.setViewport(0, 0, w, h);
    device.clear(Vec4(0.0f), std::nullopt); // the depth stays the scene's
    return {};
}
} // namespace g7::render

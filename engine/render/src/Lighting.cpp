#include <g7/core/Log.hpp>
#include <g7/render/Lighting.hpp>

#include <algorithm>

namespace g7::render
{
f32 pointLightAttenuation(f32 distance, f32 radius) noexcept
{
    if (radius <= 0.0f || distance >= radius)
    {
        return 0.0f;
    }
    const f32 ratio = distance / radius;
    const f32 window = std::clamp(1.0f - ratio * ratio * ratio * ratio, 0.0f, 1.0f);
    return window * window / (distance * distance + 1.0f);
}

void LightList::clear() noexcept
{
    m_lights.clear();
    m_warned = false;
}

void LightList::add(const PointLight& light)
{
    if (m_lights.size() >= kMaxPerFrame)
    {
        if (!m_warned)
        {
            G7_LOG_WARN("render", "more than {} point lights this frame; extra lights are ignored",
                        kMaxPerFrame);
            m_warned = true;
        }
        return;
    }
    m_lights.push_back(light);
}

void LightList::selectFor(const AABB& bounds, std::vector<u32>& out) const
{
    struct Candidate
    {
        f32 distance;
        u32 index;
    };
    std::vector<Candidate> candidates;
    for (u32 i = 0; i < m_lights.size(); ++i)
    {
        const PointLight& light = m_lights[i];
        const Vec3 closest = glm::clamp(light.position, bounds.min, bounds.max);
        const f32 distance = glm::length(light.position - closest);
        if (distance < light.radius && light.intensity > 0.0f)
        {
            candidates.push_back({distance, i});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    out.clear();
    for (usize i = 0; i < candidates.size() && i < kMaxPerObject; ++i)
    {
        out.push_back(candidates[i].index);
    }
}

GpuLighting packLighting(const Environment& environment, const LightList& lights) noexcept
{
    GpuLighting gpu{};
    const f32 length = glm::length(environment.sunDirection);
    const Vec3 sun = length > 0.0f ? environment.sunDirection / length : Vec3(0, 1, 0);
    gpu.sunDirection = Vec4(sun, 0.0f);
    gpu.sunColor = Vec4(environment.sunColor * environment.sunIntensity, 0.0f);
    gpu.ambientSky = Vec4(environment.ambientSky, 0.0f);
    gpu.ambientGround = Vec4(environment.ambientGround, 0.0f);
    const auto& points = lights.lights();
    gpu.counts[0] = static_cast<i32>(points.size());
    for (usize i = 0; i < points.size(); ++i)
    {
        gpu.pointPositionRadius[i] = Vec4(points[i].position, points[i].radius);
        gpu.pointColor[i] = Vec4(points[i].color * points[i].intensity, 0.0f);
    }
    return gpu;
}
} // namespace g7::render

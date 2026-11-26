#include <g7/core/Profiler.hpp>
#include <g7/render/ReflectionProbes.hpp>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace g7::render
{
namespace
{
constexpr u8 kAllFaces = (1u << kProbeFaces) - 1u;

bool farFromAll(const std::vector<Vec3>& placed, const Vec3& point, f32 merge)
{
    const f32 merge2 = merge * merge;
    return std::ranges::none_of(placed,
                                [&](const Vec3& p) { return glm::dot(p - point, p - point) < merge2; });
}
} // namespace

std::vector<Vec3> placeReflectionProbes(std::span<const Vec3> markers, std::span<const Vec3> junctions,
                                        std::span<const std::pair<Vec3, Vec3>> segments,
                                        const ProbePlacement& placement)
{
    // Distances are compared on the ground: the markers are lowered by the height for that and raised again.
    const Vec3 up(0.0f, placement.height, 0.0f);
    std::vector<Vec3> ground;
    for (const Vec3& marker : markers)
    {
        ground.push_back(marker - up);
    }
    const auto offer = [&](const Vec3& p)
    {
        if (farFromAll(ground, p, placement.merge))
        {
            ground.push_back(p);
        }
    };
    for (const Vec3& junction : junctions)
    {
        offer(junction);
    }
    const f32 spacing = std::max(placement.spacing, 0.5f);
    for (const auto& [a, b] : segments)
    {
        const f32 length = glm::length(b - a);
        const u32 steps = static_cast<u32>(std::ceil(length / spacing));
        for (u32 i = 0; i <= steps; ++i)
        {
            offer(steps == 0 ? a : glm::mix(a, b, static_cast<f32>(i) / static_cast<f32>(steps)));
        }
    }
    for (Vec3& p : ground)
    {
        p += up;
    }
    return ground;
}

Camera probeFaceCamera(const Vec3& position, u32 face, f32 range) noexcept
{
    const Vec3 forward = kProbeFaceForward[face % kProbeFaces];
    const Vec3 up = kProbeFaceUp[face % kProbeFaces];
    const Vec3 right = glm::cross(forward, up);
    Camera camera;
    camera.transform.position = position;
    // Camera space: x right, y up, looking along -z.
    camera.transform.rotation = glm::quat_cast(glm::mat3(right, up, -forward));
    camera.fovY = glm::half_pi<f32>();
    camera.aspect = 1.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = std::max(range, 1.0f);
    return camera;
}

void ProbeSchedule::setProbes(std::vector<ReflectionProbe> probes)
{
    m_probes = std::move(probes);
    m_slotOf.assign(m_probes.size(), ~0u);
    m_slots.assign(m_settings.slots, Slot{});
    m_shader.clear();
}

void ProbeSchedule::invalidate() noexcept
{
    for (Slot& slot : m_slots)
    {
        slot.stale = slot.probe != ~0u ? kAllFaces : 0;
    }
}

void ProbeSchedule::update(const Vec3& eye, u32 budget, std::vector<ProbeFace>& out)
{
    out.clear();
    if (m_slots.size() != m_settings.slots)
    {
        m_slots.assign(m_settings.slots, Slot{});
        m_slotOf.assign(m_probes.size(), ~0u);
    }
    // The probes within the radius, nearest first (ties: lower index), at most one per slot.
    m_near.clear();
    const f32 radius2 = m_settings.radius * m_settings.radius;
    for (u32 i = 0; i < m_probes.size(); ++i)
    {
        const Vec3 d = m_probes[i].position - eye;
        if (const f32 d2 = glm::dot(d, d); d2 <= radius2)
        {
            m_near.emplace_back(d2, i);
        }
    }
    std::ranges::sort(m_near);
    if (m_near.size() > m_slots.size())
    {
        m_near.resize(m_slots.size());
    }
    // Probes no longer wanted give their slots back; the wanted ones without a slot take free ones.
    for (u32 s = 0; s < m_slots.size(); ++s)
    {
        Slot& slot = m_slots[s];
        if (slot.probe != ~0u &&
            std::ranges::none_of(m_near, [&](const auto& n) { return n.second == slot.probe; }))
        {
            m_slotOf[slot.probe] = ~0u;
            slot = Slot{};
        }
    }
    u32 freeSlot = 0;
    for (const auto& [d2, probe] : m_near)
    {
        if (m_slotOf[probe] != ~0u)
        {
            continue;
        }
        while (freeSlot < m_slots.size() && m_slots[freeSlot].probe != ~0u)
        {
            ++freeSlot;
        }
        if (freeSlot == m_slots.size())
        {
            break;
        }
        m_slots[freeSlot] = Slot{probe, 0, 0};
        m_slotOf[probe] = freeSlot;
    }
    // Due faces, nearest probe first: never drawn before stale ones of the same probe, in face order.
    for (const auto& [d2, probe] : m_near)
    {
        if (out.size() >= budget)
        {
            break;
        }
        const u32 s = m_slotOf[probe];
        const u8 due = static_cast<u8>((~m_slots[s].drawn & kAllFaces) | m_slots[s].stale);
        for (u32 f = 0; f < kProbeFaces && out.size() < budget; ++f)
        {
            if ((due >> f) & 1u)
            {
                out.push_back({probe, s, f, tile(s, f)});
            }
        }
    }
    // For the shader: the finished probes, nearest first.
    m_shader.clear();
    for (const auto& [d2, probe] : m_near)
    {
        if (m_shader.size() >= kMaxShaderProbes)
        {
            break;
        }
        const u32 s = m_slotOf[probe];
        if (s != ~0u && m_slots[s].drawn == kAllFaces)
        {
            const ReflectionProbe& p = m_probes[probe];
            m_shader.push_back(
                {Vec4(p.position, static_cast<f32>(s)), Vec4(p.box.min, 0.0f), Vec4(p.box.max, 0.0f)});
        }
    }
}

void ProbeSchedule::faceDone(const ProbeFace& face) noexcept
{
    if (face.slot >= m_slots.size() || m_slots[face.slot].probe != face.probe)
    {
        return; // the slot went to another probe meanwhile
    }
    Slot& slot = m_slots[face.slot];
    slot.drawn = static_cast<u8>(slot.drawn | (1u << face.face));
    slot.stale = static_cast<u8>(slot.stale & ~(1u << face.face));
}

PixelRect ProbeSchedule::tile(u32 slot, u32 face) const noexcept
{
    const u32 size = m_settings.faceSize;
    return {static_cast<i32>(((slot % slotsPerRow()) * kProbeFaces + face) * size),
            static_cast<i32>((slot / slotsPerRow()) * size), size, size};
}

u32 ProbeSchedule::slotsUsed() const noexcept
{
    return static_cast<u32>(std::ranges::count_if(m_slots, [](const Slot& s) { return s.probe != ~0u; }));
}

u32 ProbeSchedule::slotsReady() const noexcept
{
    return static_cast<u32>(
        std::ranges::count_if(m_slots, [](const Slot& s) { return s.probe != ~0u && s.drawn == kAllFaces; }));
}

Result<ReflectionProbes> ReflectionProbes::create(Device& device, const ProbeSettings& settings)
{
    ReflectionProbes probes;
    probes.m_schedule = ProbeSchedule(settings);
    const u32 rows = (settings.slots + probes.m_schedule.slotsPerRow() - 1) / probes.m_schedule.slotsPerRow();
    const u32 width = probes.m_schedule.slotsPerRow() * kProbeFaces * settings.faceSize;
    const u32 height = std::max(rows, 1u) * settings.faceSize;
    auto color = device.createTexture({width, height, rhi::Format::RGBA16F, 1});
    if (!color)
    {
        return color.error();
    }
    auto depth = device.createTexture({width, height, rhi::Format::Depth32F, 1});
    if (!depth)
    {
        return depth.error();
    }
    probes.m_color = std::move(color).value();
    probes.m_depth = std::move(depth).value();
    auto framebuffer = device.createFramebuffer({{&probes.m_color}, &probes.m_depth});
    if (!framebuffer)
    {
        return framebuffer.error();
    }
    probes.m_framebuffer = std::move(framebuffer).value();
    device.bindFramebuffer(&probes.m_framebuffer);
    device.setViewport(0, 0, width, height);
    device.clear(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    device.bindFramebuffer(nullptr);
    probes.m_ready = true;
    return probes;
}

void ReflectionProbes::beginFace(Device& device, const ProbeFace& face) const
{
    G7_PROFILE_SCOPE("render::probe face");
    device.bindFramebuffer(&m_framebuffer);
    device.setViewport(face.tile.x, face.tile.y, face.tile.width, face.tile.height);
    device.clearDepthRegion(m_depth, face.tile, 0.0f); // reverse-Z: 0 is far
    device.setScissor(face.tile);
}

void ReflectionProbes::endFace(Device& device, const ProbeFace& face)
{
    device.setScissor(std::nullopt);
    m_schedule.faceDone(face);
}

Vec4 ReflectionProbes::shaderParams() const noexcept
{
    return Vec4(static_cast<f32>(m_schedule.shaderProbes().size()),
                static_cast<f32>(m_schedule.slotsPerRow()), static_cast<f32>(m_schedule.settings().faceSize),
                0.0f);
}
} // namespace g7::render

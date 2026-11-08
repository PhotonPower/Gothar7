#include <g7/render/Wind.hpp>

#include <algorithm>
#include <cmath>

namespace g7::render
{
namespace
{
f32 fract(f32 x) noexcept
{
    return x - std::floor(x);
}

f32 hash(Vec2 cell) noexcept
{
    return fract(std::sin(glm::dot(cell, Vec2(127.1f, 311.7f))) * 43758.5453f);
}

/// The phase of an instance's sway, from where it stands.
f32 phaseOf(const Mat4& model) noexcept
{
    return glm::dot(Vec2(model[3].x, model[3].z), Vec2(0.37f, 0.71f));
}
} // namespace

WindBend windBend(const asset::MaterialInfo& material, const AABB& modelBounds) noexcept
{
    WindBend bend;
    bend.kind = material.wind;
    bend.strength = material.windStrength;
    bend.foot = modelBounds.min.y;
    bend.height = std::max(modelBounds.max.y - modelBounds.min.y, 0.01f);
    bend.pivot = material.windPivot;
    const f32 x = modelBounds.max.x - modelBounds.min.x;
    const f32 z = modelBounds.max.z - modelBounds.min.z;
    if (bend.kind == asset::WindKind::Hanging && std::max(x, z) > 2.0f * std::min(x, z))
    {
        bend.hinge = x > z ? 1 : 2;
    }
    return bend;
}

f32 windNoise(Vec2 p) noexcept
{
    const Vec2 cell(std::floor(p.x), std::floor(p.y));
    const Vec2 f = p - cell;
    const Vec2 u = f * f * (Vec2(3.0f) - 2.0f * f);
    const f32 a = hash(cell);
    const f32 b = hash(cell + Vec2(1.0f, 0.0f));
    const f32 c = hash(cell + Vec2(0.0f, 1.0f));
    const f32 d = hash(cell + Vec2(1.0f, 1.0f));
    return glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
}

f32 windAt(const WindFrame& frame, Vec2 xz) noexcept
{
    const f32 gust =
        windNoise(xz * kWindGustScale - frame.direction * (frame.time * kWindGustSpeed * kWindGustScale));
    return std::clamp(frame.strength * (1.0f + frame.gusts * 1.5f * (2.0f * gust - 1.0f)), 0.0f, 1.0f);
}

f32 windWeight(const WindBend& bend, f32 y) noexcept
{
    const f32 w = std::clamp((y - bend.foot) / bend.height, 0.0f, 1.0f);
    switch (bend.kind)
    {
    case asset::WindKind::Trunk:
    case asset::WindKind::Leaves:
        return w * w;
    case asset::WindKind::Grass:
    case asset::WindKind::Cloth:
        return w;
    case asset::WindKind::Hanging:
    case asset::WindKind::None:
        break;
    }
    return 0.0f;
}

Vec3 windBent(const WindFrame& frame, const WindBend& bend, const Mat4& model,
              const Vec3& modelPosition) noexcept
{
    const Vec3 world = Vec3(model * Vec4(modelPosition, 1.0f));
    if (bend.kind == asset::WindKind::None || frame.strength <= 0.0f)
    {
        return world;
    }
    const Vec3 origin(model[3]);
    const f32 s = windAt(frame, Vec2(origin.x, origin.z)) * bend.strength;
    const f32 phase = phaseOf(model);
    const f32 t = frame.time;
    const Vec3 dir(frame.direction.x, 0.0f, frame.direction.y);
    if (bend.kind == asset::WindKind::Hanging)
    {
        // Rigid about the pivot: the displacement grows with the distance below it (the contract's weight
        // (pivot_y - y) / extent).
        f32 angle = glm::radians(kWindHangingDegrees) * s * (0.8f + 0.2f * std::sin(t * 1.7f + phase));
        const Vec3 pivot = Vec3(model * Vec4(bend.pivot, 1.0f));
        const Vec3 up(0.0f, 1.0f, 0.0f);
        Vec3 axis = glm::cross(dir, up); // free: the bottom swings downwind
        if (bend.hinge != 0)
        {
            // Hinged: only the wind across the hinge swings it (sign changes with the side it comes from).
            const Vec3 hinge = Vec3(model * Vec4(bend.hinge == 1 ? Vec3(1, 0, 0) : Vec3(0, 0, 1), 0.0f));
            axis = glm::normalize(Vec3(hinge.x, 0.0f, hinge.z));
            angle *= glm::dot(dir, glm::cross(up, axis));
        }
        const Vec3 r = world - pivot;
        const f32 c = std::cos(angle);
        const f32 sn = std::sin(angle);
        return pivot + r * c + glm::cross(axis, r) * sn + axis * glm::dot(axis, r) * (1.0f - c);
    }
    const f32 heightWorld = bend.height * glm::length(Vec3(model[1]));
    const f32 w = windWeight(bend, modelPosition.y);
    Vec3 offset(0.0f);
    switch (bend.kind)
    {
    case asset::WindKind::Trunk:
    case asset::WindKind::Leaves:
        offset = dir * (kWindTreeBend * heightWorld * w * s * (0.75f + 0.25f * std::sin(t * 1.1f + phase)));
        if (bend.kind == asset::WindKind::Leaves)
        {
            // Flutter: a few centimetres, quick, different on every leaf.
            const f32 flutter = std::sin(t * 7.0f + glm::dot(world, Vec3(3.1f, 2.3f, 2.7f)));
            offset += Vec3(-dir.z, 0.6f, dir.x) * (0.03f * w * s * flutter);
        }
        break;
    case asset::WindKind::Grass:
    case asset::WindKind::Cloth:
    {
        const f32 local = phase + glm::dot(Vec2(world.x, world.z), Vec2(1.7f, 1.3f));
        const f32 amount = bend.kind == asset::WindKind::Grass ? kWindGrassBend : kWindClothBend;
        offset = dir * (amount * heightWorld * w * s * (0.7f + 0.3f * std::sin(t * 2.3f + local)));
        if (bend.kind == asset::WindKind::Cloth)
        {
            offset +=
                Vec3(-dir.z, 0.0f, dir.x) * (0.05f * heightWorld * w * s * std::sin(t * 9.0f + local * 3.0f));
        }
        break;
    }
    case asset::WindKind::Hanging:
    case asset::WindKind::None:
        break;
    }
    return world + offset;
}

Vec3 windVelocity(const WindFrame& frame, const Vec3& position) noexcept
{
    const f32 s = windAt(frame, Vec2(position.x, position.z));
    return Vec3(frame.direction.x, 0.0f, frame.direction.y) * (kWindParticleSpeed * s);
}

GpuWind packWind(const WindFrame& frame) noexcept
{
    GpuWind gpu;
    gpu.state = Vec4(frame.direction, frame.strength, frame.gusts);
    gpu.time = Vec4(frame.time, 0.0f, 0.0f, 0.0f);
    return gpu;
}
} // namespace g7::render

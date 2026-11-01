#include <g7/gameplay/WeaponSweep.hpp>

#include <algorithm>
#include <cmath>

namespace g7::gameplay
{
namespace
{
constexpr u32 kMaxSubSteps = 32;

Vec3 middle(const Blade& b) noexcept
{
    return (b.grip + b.tip) * 0.5f;
}
} // namespace

Blade lerpBlade(const Blade& a, const Blade& b, f32 t) noexcept
{
    const Vec3 da = a.tip - a.grip;
    const Vec3 db = b.tip - b.grip;
    const f32 la = glm::length(da);
    const f32 lb = glm::length(db);
    Blade out;
    out.grip = glm::mix(a.grip, b.grip, t);
    Vec3 direction = la > 1e-6f && lb > 1e-6f ? glm::mix(da / la, db / lb, t) : glm::mix(da, db, t);
    const f32 dl = glm::length(direction);
    if (la > 1e-6f && lb > 1e-6f && dl > 1e-4f)
    {
        direction = direction / dl * glm::mix(la, lb, t); // turn, keep the length
    }
    else
    {
        direction = glm::mix(da, db, t); // opposite or degenerate: straight
    }
    out.tip = out.grip + direction;
    return out;
}

f32 segmentDistance(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1) noexcept
{
    const Vec3 d1 = p1 - p0;
    const Vec3 d2 = q1 - q0;
    const Vec3 r = p0 - q0;
    const f32 a = glm::dot(d1, d1);
    const f32 e = glm::dot(d2, d2);
    const f32 f = glm::dot(d2, r);
    f32 s = 0.0f;
    f32 t = 0.0f;
    if (a <= 1e-8f && e <= 1e-8f)
    {
        return glm::length(r);
    }
    if (a <= 1e-8f)
    {
        t = std::clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const f32 c = glm::dot(d1, r);
        if (e <= 1e-8f)
        {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const f32 b = glm::dot(d1, d2);
            const f32 denom = a * e - b * b;
            s = denom != 0.0f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f)
            {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    return glm::length((p0 + d1 * s) - (q0 + d2 * t));
}

SweepTarget uprightTarget(u32 id, const Vec3& feet, f32 height, f32 radius) noexcept
{
    // The capsule spans feet .. feet + height: its axis is shorter by the radius at both ends.
    const f32 bottom = std::min(radius, height * 0.5f);
    const f32 top = std::max(height - radius, bottom);
    return {id, feet + Vec3(0.0f, bottom, 0.0f), feet + Vec3(0.0f, top, 0.0f), radius};
}

SweepTarget bodyTarget(u32 id, const BodyShape& shape, const Vec3& feet, const Vec3& forward) noexcept
{
    const Vec3 ahead = glm::length(Vec2(forward.x, forward.z)) > 1e-6f
                           ? glm::normalize(Vec3(forward.x, 0.0f, forward.z))
                           : Vec3(0.0f, 0.0f, -1.0f);
    const Vec3 right(-ahead.z, 0.0f, ahead.x);
    const Vec3 middle =
        feet + right * shape.offset.x + Vec3(0.0f, shape.offset.y, 0.0f) + ahead * shape.offset.z;
    const Vec3 axis = shape.lying ? ahead : Vec3(0.0f, 1.0f, 0.0f);
    const f32 half = std::max(shape.length * 0.5f - shape.radius, 0.0f);
    return {id, middle - axis * half, middle + axis * half, shape.radius};
}

f32 groundDistanceToAxis(const Vec3& point, const SweepTarget& target) noexcept
{
    const Vec3 p(point.x, 0.0f, point.z);
    return segmentDistance(p, p, Vec3(target.a.x, 0.0f, target.a.z), Vec3(target.b.x, 0.0f, target.b.z));
}

bool bladeTouches(const Blade& blade, f32 radius, const SweepTarget& target) noexcept
{
    return segmentDistance(blade.grip, blade.tip, target.a, target.b) <= radius + target.radius;
}

SweepResult sweepBlade(const Blade& from, const Blade& to, f32 radius, std::span<const SweepTarget> targets,
                       const BladeBlocker& blocker, f32 maxSubStep)
{
    SweepResult result;
    const auto test = [&](const Blade& pose)
    {
        for (const SweepTarget& t : targets)
        {
            if (std::ranges::find(result.hits, t.id) == result.hits.end() && bladeTouches(pose, radius, t))
            {
                result.hits.push_back(t.id);
            }
        }
    };
    const f32 travel = std::max(glm::length(to.grip - from.grip), glm::length(to.tip - from.tip));
    const u32 steps =
        std::clamp(static_cast<u32>(std::ceil(travel / std::max(maxSubStep, 1e-3f))), 1u, kMaxSubSteps);
    Blade previous = from;
    for (u32 k = 1; k <= steps; ++k)
    {
        Blade next = lerpBlade(from, to, static_cast<f32>(k) / static_cast<f32>(steps));
        const std::optional<f32> stop =
            blocker ? blocker(previous, radius, middle(next) - middle(previous)) : std::nullopt;
        if (stop && *stop <= 0.0f)
        {
            result.blocked = true; // in the wall already: from here on it hits nothing
            return result;
        }
        if (k == 1)
        {
            test(previous);
        }
        if (stop)
        {
            result.blocked = true;
            test(lerpBlade(previous, next, std::clamp(*stop, 0.0f, 1.0f)));
            return result;
        }
        test(next);
        previous = next;
    }
    return result;
}
} // namespace g7::gameplay

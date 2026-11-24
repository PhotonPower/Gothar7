#include <g7/world/Movers.hpp>

#include <algorithm>
#include <cmath>

namespace g7::world
{
MoverMotion moverMotion(const Mover& mover, const AABB& modelBounds, const Vec3& scale) noexcept
{
    const Vec3 size = (modelBounds.max - modelBounds.min) * glm::abs(scale);
    MoverMotion motion;
    switch (mover.kind)
    {
    case Mover::Kind::Gate:
        motion.offset = Vec3(0.0f, size.y, 0.0f);
        break;
    case Mover::Kind::SecretWall:
        motion.offset = Vec3(0.0f, 0.0f, -size.z);
        break;
    case Mover::Kind::Trapdoor:
        motion.axis = Vec3(1.0f, 0.0f, 0.0f);
        motion.angle = glm::radians(90.0f);
        break;
    }
    if (mover.offset)
    {
        motion.offset = *mover.offset;
    }
    if (mover.axis && glm::length(*mover.axis) > 1e-6f)
    {
        motion.axis = glm::normalize(*mover.axis);
    }
    if (mover.angle)
    {
        motion.angle = glm::radians(*mover.angle);
    }
    return motion;
}

Transform moverPose(const Transform& closed, const MoverMotion& motion, f32 position) noexcept
{
    const f32 t = std::clamp(position, 0.0f, 1.0f);
    const f32 eased = t * t * (3.0f - 2.0f * t);
    Transform pose = closed;
    pose.position = closed.position + closed.rotation * (motion.offset * eased);
    if (motion.angle != 0.0f)
    {
        pose.rotation = closed.rotation * glm::angleAxis(motion.angle * eased, motion.axis);
    }
    return pose;
}

bool MoverCourse::advance(f32 seconds, f32 wayTime) noexcept
{
    if (!moving())
    {
        return false;
    }
    const f32 step = wayTime > 0.0f ? seconds / wayTime : 1.0f;
    position = target > position ? std::min(position + step, target) : std::max(position - step, target);
    return !moving();
}

OrientedBox orientedBox(const AABB& local, const Mat4& world) noexcept
{
    OrientedBox box;
    box.center = Vec3(world * Vec4(local.center(), 1.0f));
    const Vec3 extents = local.extents();
    for (int i = 0; i < 3; ++i)
    {
        const Vec3 column(world[i]);
        const f32 length = glm::length(column);
        box.axes[static_cast<usize>(i)] = length > 1e-8f ? column / length : Vec3(0.0f);
        box.half[i] = extents[i] * length;
    }
    return box;
}

f32 distanceTo(const OrientedBox& box, const Vec3& point) noexcept
{
    const Vec3 d = point - box.center;
    f32 squared = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        const f32 along = glm::dot(d, box.axes[static_cast<usize>(i)]);
        const f32 outside = std::max(std::abs(along) - box.half[i], 0.0f);
        squared += outside * outside;
    }
    return std::sqrt(squared);
}

bool capsuleInBox(const OrientedBox& box, const Vec3& feet, f32 radius, f32 height, f32 margin) noexcept
{
    // Points along the capsule's axis, no further apart than a third of the radius: one nearer to the box
    // than the radius (less the margin) means the capsule reaches into it.
    const f32 bottom = feet.y + radius;
    const f32 top = feet.y + std::max(height - radius, radius);
    const f32 reach = radius - margin;
    const int steps =
        std::max(1, static_cast<int>(std::ceil((top - bottom) / std::max(radius / 3.0f, 0.01f))));
    for (int i = 0; i <= steps; ++i)
    {
        const f32 y = bottom + (top - bottom) * static_cast<f32>(i) / static_cast<f32>(steps);
        if (distanceTo(box, Vec3(feet.x, y, feet.z)) < reach)
        {
            return true;
        }
    }
    return false;
}

bool segmentCrossesBox(const OrientedBox& box, const Vec3& a, const Vec3& b) noexcept
{
    // Slabs in the box's frame.
    f32 enter = 0.0f;
    f32 leave = 1.0f;
    const Vec3 from = a - box.center;
    const Vec3 way = b - a;
    for (int i = 0; i < 3; ++i)
    {
        const Vec3& axis = box.axes[static_cast<usize>(i)];
        const f32 start = glm::dot(from, axis);
        const f32 along = glm::dot(way, axis);
        if (std::abs(along) < 1e-8f)
        {
            if (std::abs(start) > box.half[i])
            {
                return false;
            }
            continue;
        }
        f32 t0 = (-box.half[i] - start) / along;
        f32 t1 = (box.half[i] - start) / along;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave)
        {
            return false;
        }
    }
    return true;
}

bool moverBlocks(Mover::Kind kind, const MoverCourse& course) noexcept
{
    return kind == Mover::Kind::Trapdoor ? course.position > 0.0f || course.target > 0.0f
                                         : course.position < 1.0f || course.target < 1.0f;
}
} // namespace g7::world

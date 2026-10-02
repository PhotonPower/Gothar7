#include <g7/core/Math.hpp>

#include <cmath>

namespace g7
{
bool nearlyEqual(f32 a, f32 b, f32 epsilon) noexcept
{
    return std::abs(a - b) <= epsilon;
}

bool nearlyEqual(const Vec3& a, const Vec3& b, f32 epsilon) noexcept
{
    return nearlyEqual(a.x, b.x, epsilon) && nearlyEqual(a.y, b.y, epsilon) && nearlyEqual(a.z, b.z, epsilon);
}

bool nearlyEqual(const Quat& a, const Quat& b, f32 epsilon) noexcept
{
    // |dot| == 1 for identical rotations, regardless of the sign ambiguity.
    return std::abs(glm::dot(a, b)) >= 1.0f - epsilon;
}

Quat quatFromEuler(f32 pitch, f32 yaw, f32 roll) noexcept
{
    const Quat qYaw = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
    const Quat qPitch = glm::angleAxis(pitch, Vec3(1.0f, 0.0f, 0.0f));
    const Quat qRoll = glm::angleAxis(roll, Vec3(0.0f, 0.0f, 1.0f));
    return glm::normalize(qYaw * qPitch * qRoll);
}

Quat lookRotation(const Vec3& forward, const Vec3& up) noexcept
{
    const f32 length = glm::length(forward);
    if (length < kEpsilon)
    {
        return Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const Vec3 direction = forward / length;
    Vec3 safeUp = up;
    if (std::abs(glm::dot(direction, glm::normalize(up))) > 1.0f - kEpsilon)
    {
        // Looking straight up or down: any perpendicular axis gives a valid basis.
        safeUp = std::abs(direction.z) < 0.9f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(1.0f, 0.0f, 0.0f);
    }
    return glm::normalize(glm::quatLookAtRH(direction, safeUp));
}
} // namespace g7

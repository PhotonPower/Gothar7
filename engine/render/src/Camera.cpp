#include <g7/render/Camera.hpp>

#include <algorithm>
#include <cmath>

namespace g7::render
{
Mat4 Camera::view() const noexcept
{
    // The camera's scale is irrelevant for viewing; only position and rotation count.
    Transform rigid;
    rigid.position = transform.position;
    rigid.rotation = transform.rotation;
    return rigid.inverse().toMatrix();
}

Mat4 Camera::projection() const noexcept
{
    return perspectiveReverseZ(fovY, aspect > 0.0f ? aspect : 1.0f, nearPlane, farPlane);
}

namespace
{
constexpr f32 kMaxPitch = toRadians(89.0f);
} // namespace

void FreeFlyCamera::attach(const Camera& camera) noexcept
{
    const Vec3 forward = camera.transform.forward();
    m_pitch = std::clamp(std::asin(std::clamp(forward.y, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
    // Yaw 0 looks along -Z, positive yaw turns towards -X.
    m_yaw = std::atan2(-forward.x, -forward.z);
}

void FreeFlyCamera::update(Camera& camera, const FreeFlyInput& input, f64 deltaSeconds) noexcept
{
    const f32 dt = static_cast<f32>(deltaSeconds);
    m_yaw += input.turn * turnRate * dt - input.lookDelta.x * sensitivity;
    m_pitch = std::clamp(m_pitch - input.lookDelta.y * sensitivity, -kMaxPitch, kMaxPitch);
    m_yaw = std::remainder(m_yaw, 2.0f * kPi);
    camera.transform.rotation = quatFromEuler(m_pitch, m_yaw, 0.0f);

    Vec3 direction = camera.transform.forward() * input.move.z + camera.transform.right() * input.move.x +
                     kWorldUp * input.move.y;
    const f32 length = glm::length(direction);
    if (length > 1.0f)
    {
        direction /= length; // diagonal input is not faster
    }
    camera.transform.position += direction * (speed * (input.fast ? fastFactor : 1.0f) * dt);
}
} // namespace g7::render

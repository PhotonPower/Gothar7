#pragma once

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Transform.hpp>

namespace g7::render
{
/// Perspective camera. Looks along its transform's forward (-Z), +Y up. Projection is reverse-Z
/// (ADR 0002, see perspectiveReverseZ).
struct Camera
{
    Transform transform;
    f32 fovY = toRadians(70.0f); ///< Vertical field of view in radians.
    f32 nearPlane = 0.1f;
    f32 farPlane = 1500.0f;
    f32 aspect = 16.0f / 9.0f; ///< Width / height of the viewport.

    [[nodiscard]] Mat4 view() const noexcept;
    [[nodiscard]] Mat4 projection() const noexcept;
    [[nodiscard]] Mat4 viewProjection() const noexcept { return projection() * view(); }
    [[nodiscard]] Frustum frustum() const noexcept { return Frustum::fromViewProjection(viewProjection()); }
};

/// Free-flying debug camera input, already mapped from actions/mouse by the caller.
struct FreeFlyInput
{
    Vec3 move{0.0f};      ///< x right, y world up, z forward; each -1..1.
    f32 turn = 0.0f;      ///< Keyboard yaw, -1..1 (+ = left), e.g. the classic turn actions.
    Vec2 lookDelta{0.0f}; ///< Mouse motion in pixels (+x right, +y down).
    bool fast = false;
};

/// Debug camera (until the player exists, M5): flies along the view direction, rises/sinks along
/// world up, mouse look with yaw/pitch (pitch limited to ±89° so the view never flips).
class FreeFlyCamera
{
public:
    f32 speed = 10.0f;                 ///< m/s
    f32 fastFactor = 5.0f;             ///< speed multiplier while `fast`
    f32 sensitivity = toRadians(0.1f); ///< radians per pixel of mouse motion
    f32 turnRate = toRadians(90.0f);   ///< radians per second for keyboard turning

    /// Takes yaw/pitch from the camera's current orientation (call after placing it).
    void attach(const Camera& camera) noexcept;
    /// Turns `camera` to yaw/pitch (radians; yaw 0 along -Z, positive left; pitch clamped to +-89 deg).
    void setOrientation(Camera& camera, f32 yaw, f32 pitch) noexcept;
    void update(Camera& camera, const FreeFlyInput& input, f64 deltaSeconds) noexcept;

    [[nodiscard]] f32 yaw() const noexcept { return m_yaw; }
    [[nodiscard]] f32 pitch() const noexcept { return m_pitch; }

private:
    f32 m_yaw = 0.0f;
    f32 m_pitch = 0.0f;
};
} // namespace g7::render

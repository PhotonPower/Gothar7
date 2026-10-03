#pragma once

// Player movement and third-person camera (M5 part C): the mechanics in C++, the values from
// data/movement.toml (game feel, decided by the project owner). Spec: docs/modules/gameplay.md.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <functional>
#include <optional>
#include <string_view>

namespace g7::gameplay
{
struct CameraSettings
{
    f32 distance = 3.0f;            ///< m behind the target
    f32 targetHeight = 1.55f;       ///< m above the feet the camera looks at
    f32 pitchDegrees = 12.0f;       ///< default downward tilt
    f32 minPitchDegrees = -40.0f;   ///< looking up
    f32 maxPitchDegrees = 60.0f;    ///< looking down
    f32 positionLag = 0.12f;        ///< s, time constant of following the target
    f32 yawLag = 0.25f;             ///< s, time constant of turning behind the figure
    f32 collisionRadius = 0.2f;     ///< m, the camera's sphere for wall tests
    f32 minDistance = 0.6f;         ///< m, never closer to the target
    f32 mousePitchPerPixel = 0.15f; ///< degrees
};

struct MovementSettings
{
    f32 runSpeed = 4.0f; ///< m/s, the default gait (Gothic: run unless walk is held)
    f32 walkSpeed = 1.6f;
    f32 sneakSpeed = 1.1f;
    f32 backwardSpeed = 1.4f;
    f32 strafeSpeed = 2.0f;
    f32 acceleration = 12.0f;      ///< m/s^2 towards a higher speed
    f32 deceleration = 16.0f;      ///< m/s^2 towards a lower speed or standstill
    f32 turnSpeedDegrees = 180.0f; ///< per second with the turn keys
    f32 mouseTurnPerPixel = 0.15f; ///< degrees
    f32 stepHeight = 0.4f;         ///< m (physics: CharacterDesc)
    f32 maxSlopeDegrees = 50.0f;
    f32 stickToFloor = 0.5f; ///< m
    CameraSettings camera;

    /// From data/movement.toml; missing keys keep their defaults, wrong types or values are errors.
    [[nodiscard]] static Result<MovementSettings> parse(std::string_view toml, std::string_view source);
};

/// What the player asks for in one fixed step (filled from the input actions by the engine).
struct MoveInput
{
    f32 forward = 0.0f;   ///< -1 (back) .. 1
    f32 strafe = 0.0f;    ///< -1 (left) .. 1
    f32 turn = 0.0f;      ///< -1 (left) .. 1, turn keys
    f32 mouseTurn = 0.0f; ///< pixels to the right since the last step
    bool walk = false;    ///< held: walk instead of run
    bool sneak = false;
};

/// Facing and horizontal velocity of the player: turns with keys and mouse, accelerates towards the
/// speed of the gait and direction asked for. Yaw 0 looks along -Z; positive yaw turns left (+Y up).
class PlayerMovement
{
public:
    /// Returns the horizontal velocity for this step (y = 0).
    Vec3 step(const MoveInput& input, f32 seconds, const MovementSettings& settings);
    void reset(f32 yaw);

    [[nodiscard]] f32 yaw() const noexcept { return m_yaw; }
    [[nodiscard]] Vec3 velocity() const noexcept { return m_velocity; }
    /// The speed the input asks for, before acceleration (for tests and the debug overlay).
    [[nodiscard]] static Vec3 wantedVelocity(const MoveInput& input, f32 yaw,
                                             const MovementSettings& settings);

private:
    f32 m_yaw = 0.0f;
    Vec3 m_velocity{0.0f};
};

/// Forward and right vectors of a yaw (horizontal).
[[nodiscard]] Vec3 forwardOf(f32 yaw) noexcept;
[[nodiscard]] Vec3 rightOf(f32 yaw) noexcept;

/// Third-person camera behind the player, Gothic style: follows position and facing with inertia, the
/// mouse tilts it up and down, and it moves closer instead of going through walls.
class ThirdPersonCamera
{
public:
    /// Distance of free space from `from` along the unit vector `direction` for a sphere of `radius`,
    /// up to `maxDistance`; nullopt = free all the way (physics: sphereCast against the world).
    using Obstruction = std::function<std::optional<f32>(const Vec3& from, const Vec3& direction, f32 radius,
                                                         f32 maxDistance)>;

    /// Behind `feet` at `yaw` without any lag (start, teleport).
    void reset(const Vec3& feet, f32 yaw, const CameraSettings& settings);
    /// Per rendered frame with the drawn (interpolated) feet.
    void update(f32 seconds, const Vec3& feet, f32 yaw, f32 mousePitchPixels, const CameraSettings& settings,
                const Obstruction& obstruction);

    [[nodiscard]] Vec3 position() const noexcept { return m_position; }
    [[nodiscard]] Quat rotation() const noexcept { return m_rotation; }
    [[nodiscard]] Vec3 target() const noexcept { return m_target; }
    [[nodiscard]] f32 pitchDegrees() const noexcept { return m_pitch; }
    [[nodiscard]] f32 yaw() const noexcept { return m_yaw; }
    /// Distance actually used this frame (shorter than settings.distance behind obstacles).
    [[nodiscard]] f32 distance() const noexcept { return m_distance; }

private:
    void place(const CameraSettings& settings, const Obstruction* obstruction);

    Vec3 m_target{0.0f};
    f32 m_yaw = 0.0f;
    f32 m_pitch = 12.0f;
    f32 m_distance = 3.0f;
    Vec3 m_position{0.0f};
    Quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
};
} // namespace g7::gameplay

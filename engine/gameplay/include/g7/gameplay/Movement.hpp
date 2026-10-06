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
    /// s, time constant of moving back out after a wall pushed the camera in (in at once, out smoothly: no
    /// popping along walls); 0: at once
    f32 returnLag = 0.0f;
};

/// Inside (a roof above the hero; welt's rooms 4-8 m wide, 2.4-2.9 m high): a closer camera, blended in.
struct IndoorCameraSettings
{
    CameraSettings camera;   ///< distance, target height, min distance, collision radius used indoors
    f32 ceiling = 4.0f;      ///< m above the feet: a roof lower than this means inside
    f32 blendSeconds = 0.5f; ///< time constant of the change between outside and inside
};

/// `outside` blended towards `inside` by `t` (0..1): distance, target height, min distance, collision radius;
/// the rest stays as outside.
[[nodiscard]] CameraSettings blendCamera(const CameraSettings& outside, const CameraSettings& inside,
                                         f32 t) noexcept;

/// Gravity of the physics world (m/s^2), for jump speeds.
inline constexpr f32 kGravity = 9.81f;

struct JumpSettings
{
    f32 standHeight = 0.9f; ///< m, top of the feet when jumping from standing or walking
    f32 runHeight = 1.1f;   ///< m, from a run (project owner: higher and so farther than from standing)
    f32 cooldown = 0.2f;    ///< s after landing before the next jump
};

struct ClimbSettings
{
    f32 lowMax = 1.0f; ///< m, ledge classes (figuren: clips t_climb_low/mid/high built for these tops)
    f32 midMax = 1.6f;
    f32 highMax = 2.2f;    ///< higher ledges cannot be reached: jump instead
    f32 reach = 0.6f;      ///< m between the character's side and the wall
    f32 lowSeconds = 0.6f; ///< duration of the climb until animations (M6) give it
    f32 midSeconds = 1.0f;
    f32 highSeconds = 1.4f;
};

struct FallSettings
{
    f32 safeHeight = 4.0f;      ///< m, no damage up to this fall height
    f32 damagePerMeter = 10.0f; ///< hit points per metre above it (hit points come with M8)
};

struct SwimSettings
{
    f32 speed = 1.6f;            ///< m/s
    f32 slowSpeed = 0.9f;        ///< with walk held
    f32 diveSpeed = 1.0f;        ///< down, sneak held
    f32 riseSpeed = 1.2f;        ///< up, jump held
    f32 floatSpeed = 0.5f;       ///< up when nothing is held under water
    f32 startDepth = 0.9f;       ///< m of water above the feet from which one swims (hip height)
    f32 eyesAboveSurface = 0.2f; ///< m, swimming at the surface
    f32 eyeHeight = 1.62f;       ///< m above the feet (figures, physics.md)
    f32 airSeconds = 30.0f;
    f32 refillSeconds = 3.0f;         ///< from empty to full at the surface
    f32 drownDamagePerSecond = 10.0f; ///< hit points while the air is gone (hit points: M8)
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
    JumpSettings jump;
    ClimbSettings climb;
    FallSettings fall;
    SwimSettings swim;
    CameraSettings camera;
    IndoorCameraSettings indoor; ///< [camera.indoor]; unset values as outside
    /// [camera.combat] (M11, K5): with the weapon drawn and an enemy locked; unset values as outside. Indoors
    /// the nearer of the two counts.
    CameraSettings combat;
    f32 combatBlendSeconds = 0.4f;

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
    bool jump = false;     ///< pressed since the last step: jump, or climb a ledge in front
    bool jumpHeld = false; ///< held: rise while diving
};

/// Upward speed that lifts the feet `height` metres.
[[nodiscard]] f32 jumpSpeed(f32 height) noexcept;
/// Hit points a fall of `height` metres costs (0 up to the safe height).
[[nodiscard]] f32 fallDamage(f32 height, const FallSettings& settings) noexcept;

enum class LedgeClass : u8
{
    Low,  ///< step up (t_climb_low)
    Mid,  ///< pull up with the arms (t_climb_mid)
    High, ///< grab from a jump (t_climb_high)
};
/// Class of a ledge `height` metres above the feet; nullopt above highMax.
[[nodiscard]] std::optional<LedgeClass> classifyLedge(f32 height, const ClimbSettings& settings) noexcept;
[[nodiscard]] f32 climbSeconds(LedgeClass ledge, const ClimbSettings& settings) noexcept;

/// The way up a ledge until animations drive it (M6): straight up to the top's height (70 % of the time),
/// then forward onto it, both eased.
struct ClimbPath
{
    Vec3 from{0.0f};
    Vec3 to{0.0f};
    f32 seconds = 1.0f;
    LedgeClass ledge = LedgeClass::Mid;

    [[nodiscard]] Vec3 at(f32 elapsed) const noexcept;
};

/// Facing and horizontal velocity of the player: turns with keys and mouse, accelerates towards the
/// speed of the gait and direction asked for. Yaw 0 looks along -Z; positive yaw turns left (+Y up).
class PlayerMovement
{
public:
    /// Returns the horizontal velocity for this step (y = 0).
    Vec3 step(const MoveInput& input, f32 seconds, const MovementSettings& settings);
    void reset(f32 yaw);
    /// Turns to `yaw` at once, keeping the speed (autopilot).
    void setYaw(f32 yaw) noexcept;

    [[nodiscard]] f32 yaw() const noexcept { return m_yaw; }
    [[nodiscard]] Vec3 velocity() const noexcept { return m_velocity; }
    /// True if moving at running pace (a jump from it uses the run height).
    [[nodiscard]] bool running(const MovementSettings& settings) const noexcept;
    /// Stops the horizontal motion, keeps the facing (climbing, landing).
    void stop() noexcept { m_velocity = Vec3(0.0f); }
    /// The speed the input asks for, before acceleration (for tests and the debug overlay).
    [[nodiscard]] static Vec3 wantedVelocity(const MoveInput& input, f32 yaw,
                                             const MovementSettings& settings);

private:
    f32 m_yaw = 0.0f;
    Vec3 m_velocity{0.0f};
};

enum class WaterMode : u8
{
    Land, ///< not in water deeper than the hips
    Swim, ///< at the surface
    Dive, ///< under water (sneak dives, jump rises, nothing floats up)
};

/// Swimming and diving (M5 part E): from the water surface over the player and the input, the 3D velocity
/// and the air. Turning stays with PlayerMovement.
class Swimmer
{
public:
    struct Step
    {
        WaterMode mode = WaterMode::Land;
        Vec3 velocity{0.0f};    ///< for CharacterController::swim (not used on land)
        f32 drownDamage = 0.0f; ///< hit points lost in this step
    };

    /// `surface`: water surface over the feet (WaterBodies::surfaceAt), nullopt without water.
    Step step(f32 seconds, f32 feetY, std::optional<f32> surface, f32 yaw, const MoveInput& input,
              const SwimSettings& settings);
    void reset(const SwimSettings& settings) noexcept;

    [[nodiscard]] WaterMode mode() const noexcept { return m_mode; }
    [[nodiscard]] f32 airSeconds() const noexcept { return m_air; }

private:
    WaterMode m_mode = WaterMode::Land;
    f32 m_air = 30.0f;
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

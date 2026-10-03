#include <g7/core/Config.hpp>
#include <g7/gameplay/Movement.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace g7::gameplay
{
namespace
{
constexpr f32 kTwoPi = 6.28318530718f;

/// Reads `key` if present: a number > 0 (or >= 0 with allowZero), else an error.
Result<void> read(const Config& config, std::string_view key, f32& value, std::string_view source,
                  bool allowZero = false, bool allowNegative = false)
{
    if (!config.contains(key))
    {
        return {};
    }
    const auto number = config.find<f64>(key);
    if (!number || !std::isfinite(*number) ||
        (!allowNegative && (*number < 0.0 || (*number == 0.0 && !allowZero))))
    {
        return Error{std::string(source) + ": '" + std::string(key) + "' must be a positive number"};
    }
    value = static_cast<f32>(*number);
    return {};
}

/// Exponential approach with time constant `tau`: frame-rate independent, never overshoots.
f32 follow(f32 seconds, f32 tau)
{
    return tau > 0.0f ? 1.0f - std::exp(-seconds / tau) : 1.0f;
}

f32 wrapAngle(f32 radians)
{
    radians = std::fmod(radians + 0.5f * kTwoPi, kTwoPi);
    return (radians < 0.0f ? radians + kTwoPi : radians) - 0.5f * kTwoPi;
}

Vec3 moveTowards(const Vec3& from, const Vec3& to, f32 maxStep)
{
    const Vec3 delta = to - from;
    const f32 length = glm::length(delta);
    return length <= maxStep || length < 1e-6f ? to : from + delta * (maxStep / length);
}
} // namespace

Result<MovementSettings> MovementSettings::parse(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    MovementSettings s;
    CameraSettings& cam = s.camera;
    const std::pair<std::string_view, f32*> positive[] = {
        {"speed.run", &s.runSpeed},
        {"speed.walk", &s.walkSpeed},
        {"speed.sneak", &s.sneakSpeed},
        {"speed.backward", &s.backwardSpeed},
        {"speed.strafe", &s.strafeSpeed},
        {"speed.acceleration", &s.acceleration},
        {"speed.deceleration", &s.deceleration},
        {"turn.keys_degrees_per_second", &s.turnSpeedDegrees},
        {"turn.mouse_degrees_per_pixel", &s.mouseTurnPerPixel},
        {"ground.step_height", &s.stepHeight},
        {"ground.max_slope_degrees", &s.maxSlopeDegrees},
        {"jump.stand_height", &s.jump.standHeight},
        {"jump.run_height", &s.jump.runHeight},
        {"swim.speed", &s.swim.speed},
        {"swim.slow_speed", &s.swim.slowSpeed},
        {"swim.dive_speed", &s.swim.diveSpeed},
        {"swim.rise_speed", &s.swim.riseSpeed},
        {"swim.float_speed", &s.swim.floatSpeed},
        {"swim.start_depth", &s.swim.startDepth},
        {"swim.eye_height", &s.swim.eyeHeight},
        {"swim.air_seconds", &s.swim.airSeconds},
        {"swim.refill_seconds", &s.swim.refillSeconds},
        {"climb.low_max", &s.climb.lowMax},
        {"climb.mid_max", &s.climb.midMax},
        {"climb.high_max", &s.climb.highMax},
        {"climb.reach", &s.climb.reach},
        {"climb.low_seconds", &s.climb.lowSeconds},
        {"climb.mid_seconds", &s.climb.midSeconds},
        {"climb.high_seconds", &s.climb.highSeconds},
        {"camera.distance", &cam.distance},
        {"camera.target_height", &cam.targetHeight},
        {"camera.collision_radius", &cam.collisionRadius},
        {"camera.min_distance", &cam.minDistance},
        {"camera.mouse_degrees_per_pixel", &cam.mousePitchPerPixel},
    };
    for (const auto& [key, value] : positive)
    {
        if (auto r = read(c, key, *value, source); !r)
        {
            return r.error();
        }
    }
    const std::pair<std::string_view, f32*> nonNegative[] = {
        {"ground.stick_to_floor", &s.stickToFloor},
        {"jump.cooldown", &s.jump.cooldown},
        {"fall.safe_height", &s.fall.safeHeight},
        {"swim.eyes_above_surface", &s.swim.eyesAboveSurface},
        {"swim.drown_damage_per_second", &s.swim.drownDamagePerSecond},
        {"fall.damage_per_meter", &s.fall.damagePerMeter},
        {"camera.position_lag", &cam.positionLag},
        {"camera.yaw_lag", &cam.yawLag},
    };
    for (const auto& [key, value] : nonNegative)
    {
        if (auto r = read(c, key, *value, source, true); !r)
        {
            return r.error();
        }
    }
    const std::pair<std::string_view, f32*> anySign[] = {
        {"camera.pitch_degrees", &cam.pitchDegrees},
        {"camera.min_pitch_degrees", &cam.minPitchDegrees},
        {"camera.max_pitch_degrees", &cam.maxPitchDegrees},
    };
    for (const auto& [key, value] : anySign)
    {
        if (auto r = read(c, key, *value, source, true, true); !r)
        {
            return r.error();
        }
    }
    if (s.maxSlopeDegrees >= 89.0f)
    {
        return Error{std::string(source) + ": 'ground.max_slope_degrees' must be below 89"};
    }
    if (!(cam.minPitchDegrees <= cam.pitchDegrees && cam.pitchDegrees <= cam.maxPitchDegrees) ||
        cam.minPitchDegrees < -89.0f || cam.maxPitchDegrees > 89.0f)
    {
        return Error{std::string(source) + ": camera pitch must satisfy -89 <= min <= pitch <= max <= 89"};
    }
    if (!(s.climb.lowMax <= s.climb.midMax && s.climb.midMax <= s.climb.highMax))
    {
        return Error{std::string(source) + ": climb heights must satisfy low_max <= mid_max <= high_max"};
    }
    if (cam.minDistance > cam.distance)
    {
        return Error{std::string(source) + ": 'camera.min_distance' exceeds 'camera.distance'"};
    }
    return s;
}

f32 jumpSpeed(f32 height) noexcept
{
    return std::sqrt(2.0f * kGravity * std::max(0.0f, height));
}

f32 fallDamage(f32 height, const FallSettings& settings) noexcept
{
    return std::max(0.0f, height - settings.safeHeight) * settings.damagePerMeter;
}

std::optional<LedgeClass> classifyLedge(f32 height, const ClimbSettings& settings) noexcept
{
    if (height <= settings.lowMax)
    {
        return LedgeClass::Low;
    }
    if (height <= settings.midMax)
    {
        return LedgeClass::Mid;
    }
    if (height <= settings.highMax)
    {
        return LedgeClass::High;
    }
    return std::nullopt;
}

f32 climbSeconds(LedgeClass ledge, const ClimbSettings& settings) noexcept
{
    switch (ledge)
    {
    case LedgeClass::Low:
        return settings.lowSeconds;
    case LedgeClass::Mid:
        return settings.midSeconds;
    default:
        return settings.highSeconds;
    }
}

Vec3 ClimbPath::at(f32 elapsed) const noexcept
{
    constexpr f32 kRise = 0.7f;
    const f32 t = std::clamp(elapsed / seconds, 0.0f, 1.0f);
    const auto ease = [](f32 x) { return x * x * (3.0f - 2.0f * x); };
    const Vec3 up(from.x, to.y, from.z);
    return t < kRise ? glm::mix(from, up, ease(t / kRise))
                     : glm::mix(up, to, ease((t - kRise) / (1.0f - kRise)));
}

bool PlayerMovement::running(const MovementSettings& settings) const noexcept
{
    return glm::length(m_velocity) > 0.5f * (settings.walkSpeed + settings.runSpeed);
}

Swimmer::Step Swimmer::step(f32 seconds, f32 feetY, std::optional<f32> surface, f32 yaw, const MoveInput& in,
                            const SwimSettings& s)
{
    Step out;
    const f32 depth = surface ? *surface - feetY : -1.0f;
    // In from hip deep; out (to land) only clearly shallower again, so the shore does not flicker.
    constexpr f32 kLeaveMargin = 0.15f;
    if (m_mode == WaterMode::Land && depth > s.startDepth)
    {
        m_mode = WaterMode::Swim;
    }
    else if (m_mode != WaterMode::Land && depth < s.startDepth - kLeaveMargin)
    {
        m_mode = WaterMode::Land;
    }
    if (m_mode != WaterMode::Land)
    {
        const f32 floating = *surface + s.eyesAboveSurface - s.eyeHeight; // feet when swimming at the top
        if (m_mode == WaterMode::Swim && in.sneak)
        {
            m_mode = WaterMode::Dive;
        }
        else if (m_mode == WaterMode::Dive && !in.sneak && feetY >= floating - 0.02f)
        {
            m_mode = WaterMode::Swim;
        }
        f32 up = 0.0f;
        if (m_mode == WaterMode::Swim)
        {
            up = std::clamp((floating - feetY) * 4.0f, -s.riseSpeed, s.riseSpeed); // buoyancy to the top
        }
        else
        {
            // Down, up, or slowly up - never above the swimming height.
            up = in.sneak ? -s.diveSpeed : in.jumpHeld ? s.riseSpeed : s.floatSpeed;
            up = std::min(up, std::max(0.0f, (floating - feetY) / std::max(seconds, 1e-4f)));
        }
        Vec3 horizontal = forwardOf(yaw) * std::clamp(in.forward, -1.0f, 1.0f) +
                          rightOf(yaw) * std::clamp(in.strafe, -1.0f, 1.0f);
        if (glm::length(horizontal) > 1.0f)
        {
            horizontal = glm::normalize(horizontal);
        }
        out.velocity = horizontal * (in.walk ? s.slowSpeed : s.speed) + Vec3(0.0f, up, 0.0f);
    }
    out.mode = m_mode;

    // Air runs out only with the eyes under water and comes back at the surface.
    const bool under = surface && feetY + s.eyeHeight < *surface;
    if (under)
    {
        m_air = std::max(0.0f, m_air - seconds);
        if (m_air <= 0.0f)
        {
            out.drownDamage = s.drownDamagePerSecond * seconds;
        }
    }
    else
    {
        m_air = std::min(s.airSeconds, m_air + s.airSeconds / std::max(s.refillSeconds, 1e-3f) * seconds);
    }
    return out;
}

void Swimmer::reset(const SwimSettings& settings) noexcept
{
    m_mode = WaterMode::Land;
    m_air = settings.airSeconds;
}

Vec3 forwardOf(f32 yaw) noexcept
{
    return Vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
}

Vec3 rightOf(f32 yaw) noexcept
{
    return Vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
}

Vec3 PlayerMovement::wantedVelocity(const MoveInput& input, f32 yaw, const MovementSettings& s)
{
    const f32 gait = input.sneak ? s.sneakSpeed : input.walk ? s.walkSpeed : s.runSpeed;
    const f32 forward = std::clamp(input.forward, -1.0f, 1.0f);
    const f32 strafe = std::clamp(input.strafe, -1.0f, 1.0f);
    // Backwards and sideways have their own speeds, never faster than the gait (sneaking stays slow).
    const f32 along = forward >= 0.0f ? forward * gait : forward * std::min(s.backwardSpeed, gait);
    const f32 side = strafe * std::min(s.strafeSpeed, gait);
    Vec3 v = forwardOf(yaw) * along + rightOf(yaw) * side;
    // Diagonal: no faster than the faster of the two parts.
    const f32 limit = std::max(std::abs(along), std::abs(side));
    const f32 speed = glm::length(v);
    if (speed > limit && speed > 1e-6f)
    {
        v *= limit / speed;
    }
    return v;
}

Vec3 PlayerMovement::step(const MoveInput& input, f32 seconds, const MovementSettings& s)
{
    m_yaw =
        wrapAngle(m_yaw - glm::radians(std::clamp(input.turn, -1.0f, 1.0f) * s.turnSpeedDegrees * seconds +
                                       input.mouseTurn * s.mouseTurnPerPixel));
    const Vec3 wanted = wantedVelocity(input, m_yaw, s);
    const bool faster = glm::length(wanted) > glm::length(m_velocity) + 1e-4f;
    m_velocity = moveTowards(m_velocity, wanted, (faster ? s.acceleration : s.deceleration) * seconds);
    return m_velocity;
}

void PlayerMovement::setYaw(f32 yaw) noexcept
{
    m_yaw = wrapAngle(yaw);
}

void PlayerMovement::reset(f32 yaw)
{
    m_yaw = wrapAngle(yaw);
    m_velocity = Vec3(0.0f);
}

void ThirdPersonCamera::reset(const Vec3& feet, f32 yaw, const CameraSettings& settings)
{
    m_target = feet + Vec3(0.0f, settings.targetHeight, 0.0f);
    m_yaw = wrapAngle(yaw);
    m_pitch = settings.pitchDegrees;
    m_distance = settings.distance;
    place(settings, nullptr);
}

void ThirdPersonCamera::update(f32 seconds, const Vec3& feet, f32 yaw, f32 mousePitchPixels,
                               const CameraSettings& settings, const Obstruction& obstruction)
{
    const Vec3 target = feet + Vec3(0.0f, settings.targetHeight, 0.0f);
    m_target += (target - m_target) * follow(seconds, settings.positionLag);
    m_yaw = wrapAngle(m_yaw + wrapAngle(yaw - m_yaw) * follow(seconds, settings.yawLag));
    m_pitch = std::clamp(m_pitch + mousePitchPixels * settings.mousePitchPerPixel, settings.minPitchDegrees,
                         settings.maxPitchDegrees);
    place(settings, obstruction ? &obstruction : nullptr);
}

void ThirdPersonCamera::place(const CameraSettings& settings, const Obstruction* obstruction)
{
    // Looking direction from yaw and pitch (positive pitch looks down); the camera sits behind the target.
    const f32 pitch = glm::radians(m_pitch);
    const Vec3 look = forwardOf(m_yaw) * std::cos(pitch) + Vec3(0.0f, -std::sin(pitch), 0.0f);
    m_distance = settings.distance;
    if (obstruction != nullptr)
    {
        if (const auto free = (*obstruction)(m_target, -look, settings.collisionRadius, settings.distance))
        {
            m_distance = std::clamp(*free, settings.minDistance, settings.distance);
        }
    }
    m_position = m_target - look * m_distance;
    m_rotation = lookRotation(look);
}
} // namespace g7::gameplay

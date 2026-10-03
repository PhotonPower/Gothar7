#pragma once

// Look-at (M6 part D): turns neck and head of an animated pose towards a target, within limits and with a
// limited turning speed. Applied after the state machine, before skinning. Settings: animgraph.toml
// `[look_at]`.

#include <g7/animation/Skeleton.hpp>
#include <g7/core/Result.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace g7::animation
{
struct LookAtSettings
{
    /// Bones that turn and their share of the turn (parents first; shares add up to 1).
    std::vector<std::pair<std::string, f32>> bones{{"neck", 0.4f}, {"head", 0.6f}};
    f32 maxYawDegrees = 70.0f;   ///< left/right
    f32 maxPitchDegrees = 35.0f; ///< up/down
    /// A target further to the side than maxYaw + this lies behind: the head goes back to the middle.
    f32 behindDegrees = 40.0f;
    f32 degreesPerSecond = 240.0f; ///< turning speed
};

class LookAt
{
public:
    LookAt() = default;
    /// Finds the bones; one missing is an error. The eye point is the last bone (head).
    [[nodiscard]] static Result<LookAt> create(const Skeleton& skeleton, const LookAtSettings& settings);

    /// Target in model space (+Z ahead, +Y up; the figure's feet at the origin); nullopt: straight ahead.
    void setTarget(std::optional<Vec3> target) noexcept { m_target = target; }
    [[nodiscard]] const std::optional<Vec3>& target() const noexcept { return m_target; }

    /// Moves yaw and pitch towards the target and turns the bones of `pose` (local transforms).
    void update(f32 seconds, const Skeleton& skeleton, Pose& pose);

    [[nodiscard]] f32 yawDegrees() const noexcept { return m_yaw; }
    [[nodiscard]] f32 pitchDegrees() const noexcept { return m_pitch; }
    [[nodiscard]] bool valid() const noexcept { return !m_bones.empty(); }

private:
    LookAtSettings m_settings;
    std::vector<std::pair<usize, f32>> m_bones;
    std::optional<Vec3> m_target;
    f32 m_yaw = 0.0f;   // degrees, positive towards +X (the figure's left)
    f32 m_pitch = 0.0f; // degrees, positive up
    std::vector<Mat4> m_modelSpace;
};
} // namespace g7::animation

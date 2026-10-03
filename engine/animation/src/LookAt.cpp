#include <g7/animation/LookAt.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7::animation
{
Result<LookAt> LookAt::create(const Skeleton& skeleton, const LookAtSettings& settings)
{
    LookAt look;
    look.m_settings = settings;
    for (const auto& [name, share] : settings.bones)
    {
        const i32 bone = skeleton.find(name);
        if (bone < 0)
        {
            return Error{std::format("look-at: bone '{}' not in the skeleton", name)};
        }
        look.m_bones.emplace_back(static_cast<usize>(bone), share);
    }
    if (look.m_bones.empty())
    {
        return Error{"look-at: no bones"};
    }
    look.m_modelSpace.resize(skeleton.size());
    return look;
}

void LookAt::update(f32 seconds, const Skeleton& skeleton, Pose& pose)
{
    if (m_bones.empty())
    {
        return;
    }
    skeleton.modelSpace(pose, m_modelSpace);
    const LookAtSettings& s = m_settings;

    // Wanted angles: from the eye bone to the target, in model space.
    f32 wantYaw = 0.0f;
    f32 wantPitch = 0.0f;
    if (m_target)
    {
        const Vec3 eye(m_modelSpace[m_bones.back().first][3]);
        const Vec3 d = *m_target - eye;
        const f32 flat = std::sqrt(d.x * d.x + d.z * d.z);
        if (flat > 1e-4f || std::abs(d.y) > 1e-4f)
        {
            wantYaw = glm::degrees(std::atan2(d.x, d.z));
            wantPitch = glm::degrees(std::atan2(d.y, flat));
            if (std::abs(wantYaw) > s.maxYawDegrees + s.behindDegrees)
            {
                wantYaw = 0.0f; // behind: back to the middle
                wantPitch = 0.0f;
            }
        }
    }
    wantYaw = std::clamp(wantYaw, -s.maxYawDegrees, s.maxYawDegrees);
    wantPitch = std::clamp(wantPitch, -s.maxPitchDegrees, s.maxPitchDegrees);
    const f32 step = s.degreesPerSecond * seconds;
    m_yaw = m_yaw < wantYaw ? std::min(m_yaw + step, wantYaw) : std::max(m_yaw - step, wantYaw);
    m_pitch = m_pitch < wantPitch ? std::min(m_pitch + step, wantPitch) : std::max(m_pitch - step, wantPitch);
    if (std::abs(m_yaw) < 1e-4f && std::abs(m_pitch) < 1e-4f)
    {
        return;
    }

    // Each bone adds its share as a model-space rotation (yaw about +Y, pitch about -X: up), on top of the
    // shares of the bones above it: new local = (turned parent)^-1 * turn * old model rotation.
    Quat applied(1.0f, 0.0f, 0.0f, 0.0f); // turn of the ancestors handled so far
    for (const auto& [bone, share] : m_bones)
    {
        const Quat turn = glm::angleAxis(glm::radians(m_yaw * share), Vec3(0, 1, 0)) *
                          glm::angleAxis(glm::radians(-m_pitch * share), Vec3(1, 0, 0));
        const Quat model = glm::normalize(glm::quat_cast(Mat3(m_modelSpace[bone])));
        const i32 parent = skeleton.parent(bone);
        const Quat parentModel =
            parent >= 0 ? glm::normalize(glm::quat_cast(Mat3(m_modelSpace[static_cast<usize>(parent)])))
                        : Quat(1.0f, 0.0f, 0.0f, 0.0f);
        const Quat total = turn * applied;
        pose[bone].rotation = glm::normalize(glm::inverse(applied * parentModel) * total * model);
        applied = total;
    }
}
} // namespace g7::animation

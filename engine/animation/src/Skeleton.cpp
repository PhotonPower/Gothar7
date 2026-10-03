#include <g7/animation/Skeleton.hpp>

#include <format>

namespace g7::animation
{
Mat4 BoneTransform::matrix() const noexcept
{
    return glm::translate(Mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(Mat4(1.0f), scale);
}

Result<Skeleton> Skeleton::create(const asset::SkeletonData& data)
{
    if (data.names.empty())
    {
        return Error{"animation: empty skeleton"};
    }
    if (data.size() > asset::kMaxBones)
    {
        return Error{std::format("animation: {} bones, at most {}", data.size(), asset::kMaxBones)};
    }
    Skeleton s;
    s.m_names = data.names;
    s.m_parents = data.parents;
    s.m_rootParent = data.rootParent;
    s.m_rest.resize(data.size());
    for (usize i = 0; i < data.size(); ++i)
    {
        if (data.parents[i] >= static_cast<i32>(i))
        {
            return Error{std::format("animation: bone '{}' comes before its parent", data.names[i])};
        }
        s.m_rest[i] = {data.translations[i], data.rotations[i], data.scales[i]};
    }
    return s;
}

i32 Skeleton::find(std::string_view name) const noexcept
{
    for (usize i = 0; i < m_names.size(); ++i)
    {
        if (m_names[i] == name)
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

void Skeleton::modelSpace(const Pose& pose, std::span<Mat4> out) const
{
    for (usize i = 0; i < m_names.size(); ++i)
    {
        const Mat4 local = pose[i].matrix();
        out[i] = m_parents[i] < 0 ? m_rootParent * local : out[static_cast<usize>(m_parents[i])] * local;
    }
}

std::vector<f32> Skeleton::maskBelow(std::string_view bone) const
{
    std::vector<f32> mask(m_names.size(), 0.0f);
    const i32 top = find(bone);
    if (top < 0)
    {
        return mask;
    }
    mask[static_cast<usize>(top)] = 1.0f;
    for (usize i = static_cast<usize>(top) + 1; i < m_names.size(); ++i)
    {
        // Parents come first: a bone is below `top` if its parent is.
        mask[i] = m_parents[i] >= 0 ? mask[static_cast<usize>(m_parents[i])] : 0.0f;
    }
    return mask;
}

namespace
{
/// Shortest-arc normalised lerp (close keys, cheap and stable).
Quat nlerp(const Quat& a, Quat b, f32 t)
{
    if (glm::dot(a, b) < 0.0f)
    {
        b = -b;
    }
    return glm::normalize(
        Quat(glm::mix(a.w, b.w, t), glm::mix(a.x, b.x, t), glm::mix(a.y, b.y, t), glm::mix(a.z, b.z, t)));
}
} // namespace

void blendPose(Pose& a, const Pose& b, f32 weight, std::span<const f32> mask)
{
    for (usize i = 0; i < a.size() && i < b.size(); ++i)
    {
        const f32 w = weight * (mask.empty() ? 1.0f : mask[i]);
        if (w <= 0.0f)
        {
            continue;
        }
        a[i].translation = glm::mix(a[i].translation, b[i].translation, w);
        a[i].rotation = nlerp(a[i].rotation, b[i].rotation, w);
        a[i].scale = glm::mix(a[i].scale, b[i].scale, w);
    }
}

void addPose(Pose& a, const Pose& b, const Pose& reference, f32 weight, std::span<const f32> mask)
{
    for (usize i = 0; i < a.size() && i < b.size() && i < reference.size(); ++i)
    {
        const f32 w = weight * (mask.empty() ? 1.0f : mask[i]);
        if (w <= 0.0f)
        {
            continue;
        }
        a[i].translation += (b[i].translation - reference[i].translation) * w;
        // In the bone's own frame: what the clip turns relative to its reference, on top of a.
        const Quat delta = glm::inverse(reference[i].rotation) * b[i].rotation;
        a[i].rotation = glm::normalize(a[i].rotation * nlerp(Quat(1.0f, 0.0f, 0.0f, 0.0f), delta, w));
    }
}
} // namespace g7::animation

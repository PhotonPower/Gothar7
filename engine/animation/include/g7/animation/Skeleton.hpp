#pragma once

// Skeleton and poses (M6 part B, ADR 0019): local bone transforms, model-space matrices, masks and
// blending. Spec: docs/modules/animation.md.

#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::animation
{
struct BoneTransform
{
    Vec3 translation{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f};

    [[nodiscard]] Mat4 matrix() const noexcept;
};

/// Local transforms, one per bone of a skeleton.
using Pose = std::vector<BoneTransform>;

class Skeleton
{
public:
    Skeleton() = default;
    /// From loaded data (asset::SkeletonData: parents before children).
    [[nodiscard]] static Result<Skeleton> create(const asset::SkeletonData& data);

    [[nodiscard]] usize size() const noexcept { return m_names.size(); }
    [[nodiscard]] i32 find(std::string_view name) const noexcept;
    [[nodiscard]] const std::string& name(usize bone) const noexcept { return m_names[bone]; }
    [[nodiscard]] i32 parent(usize bone) const noexcept { return m_parents[bone]; }
    [[nodiscard]] const Pose& restPose() const noexcept { return m_rest; }

    /// Model-space matrices of `pose` (the root bones below the armature transform).
    void modelSpace(const Pose& pose, std::span<Mat4> out) const;
    /// 1 for `bone` and everything below it, 0 elsewhere (layers: the upper body from "spine_02").
    [[nodiscard]] std::vector<f32> maskBelow(std::string_view bone) const;

private:
    std::vector<std::string> m_names;
    std::vector<i32> m_parents;
    Pose m_rest;
    Mat4 m_rootParent{1.0f};
};

/// a towards b by `weight` (0..1) per bone; `mask` (one value per bone, optional) scales the weight.
void blendPose(Pose& a, const Pose& b, f32 weight, std::span<const f32> mask = {});
/// Adds (b - reference) to a: translations add, rotations multiply - additive clips (gestures).
void addPose(Pose& a, const Pose& b, const Pose& reference, f32 weight, std::span<const f32> mask = {});
} // namespace g7::animation

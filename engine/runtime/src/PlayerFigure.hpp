#pragma once

// The hero figure (M6 part C, internal to runtime): a skinned model played by the animation state
// machine of data/anim/<rig>.animgraph.toml. Lives behind a unique_ptr - the Animator points to the
// Skeleton, so neither may move.

#include <g7/animation/Animator.hpp>
#include <g7/asset/AssetManager.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/SkinnedMesh.hpp>

#include <array>
#include <deque>
#include <string>
#include <vector>

namespace g7
{
struct PlayerFigure
{
    std::string path;      ///< VFS path of the figure (.glb)
    std::string graphPath; ///< VFS path of the animation graph
    std::string startState;
    animation::Skeleton skeleton;
    animation::Animator animator;
    std::vector<Mat4> inverseBind;
    std::vector<Mat4> modelSpace; ///< scratch
    std::vector<Mat4> bones;      ///< skinning matrices of the last fixed step
    /// GPU side (absent without a device).
    render::SkinnedMesh mesh;
    render::MaterialSet materials;
    bool uploaded = false;
    std::vector<asset::Handle<asset::TextureData>> images; ///< parallel to SkinnedModelData::images
    /// Per gameplay::LedgeClass: the climb clip's root movement from start to end (model space, +Z ahead)
    /// and its playing time; zero duration when the graph has no root-motion clip for it.
    std::array<Vec3, 3> climbRoot{};
    std::array<f32, 3> climbSeconds{};
    Vec3 climbMoved{0.0f}; ///< root motion summed up in the current climb
    f32 airSeconds = 0.0f;
    f32 fallSeconds = 0.0f;
    bool jumped = false;            ///< a jump started in this fixed step
    std::deque<std::string> events; ///< the last ones fired, newest first (debug UI)
};
} // namespace g7

#pragma once

// The hero figure (M6 parts C and D, internal to runtime): a skinned model played by the animation state
// machine of data/anim/<rig>.animgraph.toml, with face, look-at and attachments. Lives behind a unique_ptr -
// the Animator points to the Skeleton, so neither may move.

#include <g7/animation/Animator.hpp>
#include <g7/animation/Face.hpp>
#include <g7/animation/LookAt.hpp>
#include <g7/asset/AssetManager.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/SkinnedMesh.hpp>
#include <g7/runtime/Engine.hpp>

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace g7
{
/// A static model held by a socket bone (sword in the hand, torch, bow on the back).
struct FigureAttachment
{
    std::string socket;
    usize bone = 0;
    const LoadedModel* model = nullptr;
    std::unique_ptr<LoadedModel> owned; ///< procedural models (tests, debug UI)
};

struct PlayerFigure
{
    std::string path;      ///< VFS path of the figure (.glb)
    std::string graphPath; ///< VFS path of the animation graph
    std::string startState;
    animation::Skeleton skeleton;
    animation::Animator animator;
    animation::FaceAnimator face;
    animation::LookAt lookAt; ///< invalid when the skeleton lacks its bones
    std::vector<Mat4> inverseBind;
    /// Poses of the last two fixed steps (after look-at); drawn interpolated between them.
    animation::Pose posePrevious;
    animation::Pose poseNow;
    animation::Pose poseDrawn;    ///< scratch
    std::vector<Mat4> modelSpace; ///< of the pose drawn this frame (sockets use it too)
    std::vector<Mat4> bones;      ///< skinning matrices of the pose drawn this frame
    u64 drawnFrame = ~u64(0);     ///< frame whose pose modelSpace/bones hold
    /// GPU side (absent without a device).
    render::SkinnedMesh mesh;
    render::MaterialSet materials;
    bool uploaded = false;
    std::vector<asset::Handle<asset::TextureData>> images; ///< parallel to SkinnedModelData::images
    std::vector<FigureAttachment> attachments;
    std::vector<std::string> sockets; ///< bones named socket_*, for the debug UI
    std::optional<Vec3> lookTarget;   ///< world space
    /// Debug UI (F1, "Animation"): try things out.
    bool showSockets = false;
    bool lookAtCamera = false;
    std::string stickSocket; ///< socket holding the test stick, empty: none
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

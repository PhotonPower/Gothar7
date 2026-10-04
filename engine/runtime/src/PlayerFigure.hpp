#pragma once

// Animated figures (M6, internal to runtime): a skinned model played by the animation state machine of
// data/anim/<rig>.animgraph.toml - the hero (PlayerFigure: face, look-at, attachments, climbing) and the
// animals (Creature, part D3). They live behind a unique_ptr: the Animator points to the Skeleton, so neither
// may move.

#include <g7/ai/Waynet.hpp>
#include <g7/animation/Animator.hpp>
#include <g7/animation/Face.hpp>
#include <g7/animation/LookAt.hpp>
#include <g7/asset/AssetManager.hpp>
#include <g7/asset/FigureAssembly.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/gameplay/Character.hpp>
#include <g7/physics/Character.hpp>
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
/// What hero and animals share: model, animation, the poses drawn, the GPU side.
struct AnimatedFigure
{
    std::string path;      ///< VFS path of the figure (.glb or .figure.toml)
    std::string graphPath; ///< VFS path of the animation graph
    std::string startState;
    /// Figures assembled at run time (*.figure.toml): the parts worn now (D2).
    std::optional<asset::FigureManifest> manifest;
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
    std::vector<std::string> sockets;                      ///< bones named socket_*, for the debug UI
    std::deque<std::string> events;                        ///< the last ones fired, newest first (debug UI)

    virtual ~AnimatedFigure() = default;
};

/// A static model held by a socket bone (sword in the hand, torch, bow on the back).
struct FigureAttachment
{
    std::string socket;
    usize bone = 0;
    const LoadedModel* model = nullptr;
    std::unique_ptr<LoadedModel> owned; ///< procedural models (tests, debug UI)
};

struct PlayerFigure : AnimatedFigure
{
    /// Debug UI "Outfit": heads and kit pieces that fit the body `outfitFor` (paths relative to characters/).
    std::string outfitFor;
    std::vector<std::string> outfitHeads;
    std::vector<std::string> outfitGarments;
    std::string outfitError;
    std::vector<FigureAttachment> attachments;
    std::optional<Vec3> lookTarget; ///< world space
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
    bool jumped = false; ///< a jump started in this fixed step
};

/// An animal for tests and the debug UI until M9 brings monsters with AI and collision (EngineCreatures.cpp).
struct Creature
{
    u32 id = 0;
    std::string species; ///< an animal, or the Npc instance of an inserted NPC
    /// NPCs (M8 part D): values and inventory from their Npc instance (pickpocketing).
    std::unique_ptr<gameplay::Character> character;
    bool pickpocketed = false; ///< tried once (Gothic: one try per NPC)
    /// Human NPCs (M9): a capsule like the hero's, walking along a route over the waynet.
    std::optional<physics::CharacterController> body;
    std::optional<ai::Route> route;
    usize routeIndex = 0;
    std::string routeGoal; ///< the way point or freepoint walked to
    bool running = false;
    f32 stuckSeconds = 0.0f;
    u32 replans = 0;
    Vec3 progressAt{0.0f};
    std::unique_ptr<AnimatedFigure> figure;
    Vec3 position{0.0f}; ///< feet, after the last fixed step
    Vec3 positionBefore{0.0f};
    f32 yaw = 0.0f; ///< radians, 0 = facing -Z, positive left (as the player)
    f32 yawBefore = 0.0f;
    // Parameters of the graph, set by the caller (AI with M9) or the showcase.
    f32 speed = 0.0f;
    f32 turn = 0.0f;
    i32 action = 0; ///< one step: 1 attack_1, 2 attack_2, 3 hit, 4 threaten
    bool eat = false;
    bool sleep = false;
    bool dead = false;
    f32 walkSpeed = 0.0f; ///< blend points of the graph's "move" state (showcase)
    f32 runSpeed = 0.0f;
    bool showcase = false; ///< goes through all its actions in turn
    u32 showcaseStep = 0;
    f32 showcaseTime = 0.0f;
};
} // namespace g7

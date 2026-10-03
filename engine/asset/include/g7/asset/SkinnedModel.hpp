#pragma once

// Skinned models and animation clips from glTF (M6 part A, ADR 0019): the CPU-side data the animation
// module and the renderer work with. Contracts: docs/modules/animation.md (reference skeleton),
// docs/design/characters-pipeline.md (sets, events.toml §3, LOD node names §2.2, morph targets §6.1).

#include <g7/asset/MeshData.hpp>
#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// Most bones a skeleton may have (animation.md; the human reference rig has 60).
inline constexpr u32 kMaxBones = 128;

/// Bones in parent-before-child order with their local rest transform (the glTF node TRS).
struct SkeletonData
{
    std::vector<std::string> names;
    std::vector<i32> parents; ///< index of the parent bone, -1 for a root bone
    std::vector<Vec3> translations;
    std::vector<Quat> rotations;
    std::vector<Vec3> scales;
    /// Transform of the nodes above the root bones (the armature node; identity in our exports).
    Mat4 rootParent{1.0f};

    [[nodiscard]] usize size() const noexcept { return names.size(); }
    /// Index of the bone `name`, or -1.
    [[nodiscard]] i32 find(std::string_view name) const noexcept;
};

/// Position and normal offsets of one morph target, per vertex of its part.
struct MorphTargetData
{
    std::vector<Vec3> positions;
    std::vector<Vec3> normals; ///< empty if the file has none
};

/// One skinned mesh node of a model, e.g. "body_lod0": own vertices, indices and submeshes. Positions are
/// in the skin's space (glTF ignores the mesh node's transform for skinned meshes).
struct SkinnedPartData
{
    std::string node;
    std::string role; ///< node name without "_lod<n>" ("body", "head", "cloth_shirt" ...)
    u32 lod = 0;      ///< from "_lod<n>", 0 without suffix
    std::vector<Vertex> vertices;
    std::vector<std::array<u16, 4>> joints; ///< per vertex: bone indices (SkeletonData order)
    std::vector<Vec4> weights;              ///< per vertex, summing to 1
    std::vector<u32> indices;
    std::vector<Submesh> submeshes; ///< per material
    /// In file order; for heads the fixed list of characters-pipeline.md §6.1 gives the names.
    std::vector<MorphTargetData> morphs;
};

struct SkinnedModelData
{
    SkeletonData skeleton;
    std::vector<Mat4> inverseBind; ///< per bone (SkeletonData order)
    std::vector<SkinnedPartData> parts;
    std::vector<MaterialInfo> materials;
    std::vector<ImageSource> images;
    AABB bounds; ///< of all parts in the bind pose

    /// Parts of one LOD level (nearest available per role when a role has fewer levels).
    [[nodiscard]] std::vector<const SkinnedPartData*> partsForLod(u32 lod) const;
    [[nodiscard]] u32 lodCount() const noexcept;
};

/// A clip event (events.toml): fires when the playback time passes `time`.
struct ClipEvent
{
    f32 time = 0.0f; ///< seconds (frame / fps)
    std::string name;
};

/// Keys of one bone and one channel.
struct TrackData
{
    enum class Path : u8
    {
        Translation,
        Rotation,
        Scale,
    };
    std::string bone;
    Path path = Path::Rotation;
    bool step = false;        ///< step interpolation (else linear / slerp)
    std::vector<f32> times;   ///< seconds, rising
    std::vector<Vec4> values; ///< xyz for translation/scale, quaternion xyzw for rotation
};

struct ClipData
{
    std::string name; ///< glTF animation name, e.g. "none/s_walk"
    f32 duration = 0.0f;
    std::vector<TrackData> tracks;
    std::vector<ClipEvent> events; ///< by time; same time in file order

    [[nodiscard]] bool loops() const noexcept; ///< "s_" clips (characters-pipeline.md §3)
};

/// The clips of one set file (anims/human/none.glb ...), with the skeleton they were made for.
struct AnimationSetData
{
    SkeletonData skeleton;
    std::vector<ClipData> clips;

    [[nodiscard]] const ClipData* find(std::string_view name) const noexcept;
};

/// glTF with one skin: skeleton, inverse bind matrices, skinned parts (JOINTS_0/WEIGHTS_0, morph targets),
/// materials and images. Fails for files without a skin or with more than kMaxBones bones.
[[nodiscard]] Result<SkinnedModelData> loadSkinnedGltf(std::span<const u8> bytes,
                                                       const fs::Path& baseDirectory,
                                                       std::string_view debugName = "<memory>");
/// glTF animations as clips (channels to bones by node name; cubic splines are refused) and the skeleton
/// of the file's skin (or its node tree).
[[nodiscard]] Result<AnimationSetData> loadAnimationGltf(std::span<const u8> bytes,
                                                         const fs::Path& baseDirectory,
                                                         std::string_view debugName = "<memory>");
/// Adds the events of a `<set>.events.toml` (characters-pipeline.md §3, version 1) to the set's clips.
/// Unknown clips, frames outside the clip and unsorted frames are errors.
[[nodiscard]] Result<void> applyClipEvents(AnimationSetData& set, std::string_view toml,
                                           std::string_view source);
} // namespace g7::asset

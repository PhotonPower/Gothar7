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
#include <map>
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
    std::string name; ///< from the mesh's extras.targetNames ("vis_aa", "blink_l" ...); empty if unnamed
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
    /// The node's glTF primitives in file order (non-triangle ones empty): where their vertices and
    /// triangles ended up here - the assembly data of §6.2 names glTF primitives. Empty for assembled parts.
    struct Primitive
    {
        u32 firstVertex = 0;
        u32 vertexCount = 0;
        u32 firstIndex = 0; ///< into `indices` (grouped by material, the primitive's triangles stay together)
        u32 indexCount = 0;
        u32 material = 0;
    };
    std::vector<Primitive> primitives;
};

struct SkinnedModelData
{
    SkeletonData skeleton;
    std::vector<Mat4> inverseBind; ///< per bone (SkeletonData order)
    std::vector<SkinnedPartData> parts;
    std::vector<MaterialInfo> materials;
    std::vector<ImageSource> images;
    AABB bounds; ///< of all parts in the bind pose
    /// `asset.extras.gothar` of a figure part (characters-pipeline.md §6.2, format v1): what assembling
    /// figures at run time needs. Empty (version 0) for files without it.
    struct Assembly
    {
        u32 version = 0;
        std::string part; ///< "body", "head", "hair", "cloth" ... as written by gothar-chargen
        /// Per LOD node: the neck ring in loop order; per ring point its glTF vertices [primitive, vertex].
        std::map<std::string, std::vector<std::vector<std::array<u32, 2>>>, std::less<>> neck;
        struct Falloff
        {
            u32 primitive = 0;
            u32 vertex = 0;
            u32 ringPoint = 0;
            f64 weight = 0.0; // double: assemble.py computes in double
        };
        std::map<std::string, std::vector<Falloff>, std::less<>>
            falloff;            ///< body: vertices following the ring
        std::string coversBody; ///< garment: the body part it fits (path relative to characters/)
        /// Garment: per body LOD node the hidden body triangles as [primitive, first, end).
        std::map<std::string, std::vector<std::array<u32, 3>>, std::less<>> covers;
        std::vector<std::string> hides; ///< garment: roles dropped while worn ("hair", "beard")
    };
    Assembly assembly;

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
    /// Own speed of a locomotion clip in m/s (events.toml `speed`): how fast the planted foot moves back.
    /// 0 when unknown (no playback-rate matching).
    f32 speed = 0.0f;

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
/// Adds the events and clip speeds of a `<set>.events.toml` (characters-pipeline.md §3, version 1) to the
/// set's clips. Unknown clips, frames outside the clip, unsorted frames and speeds <= 0 are errors.
[[nodiscard]] Result<void> applyClipEvents(AnimationSetData& set, std::string_view toml,
                                           std::string_view source);
} // namespace g7::asset

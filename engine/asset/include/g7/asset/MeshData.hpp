#pragma once

#include <g7/core/FileSystem.hpp>
#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// Interleaved static-mesh vertex (48 bytes), the layout the renderer uploads as-is.
struct Vertex
{
    Vec3 position{0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    Vec2 uv{0.0f};
    /// xyz + handedness in w (bitangent = cross(normal, tangent) * w). From the file, or computed for
    /// primitives whose material has a normal map; all zero otherwise.
    Vec4 tangent{0.0f};
};
static_assert(sizeof(Vertex) == 48);

/// Where an image of a model comes from: a file next to the model, or encoded bytes embedded in
/// it (.glb buffer view or data: URI). Decode with decodeImage()/loadImage().
struct ImageSource
{
    std::string uri;         ///< Relative to the model file; empty if embedded.
    std::vector<u8> encoded; ///< Embedded PNG/JPEG bytes; empty if `uri` is set.
    std::string mimeType;    ///< "image/png", "image/jpeg" or empty if unknown.
};

/// glTF alpha modes: Mask = alpha test (foliage, fences), Blend = translucency.
enum class AlphaMode : u8
{
    Opaque,
    Mask,
    Blend,
};

/// Deliberately simple, stylised material (no metallic/roughness). Image indices refer to
/// MeshData::images, -1 = none.
struct MaterialInfo
{
    std::string name;
    Vec4 baseColor{1.0f};    ///< Linear factor (glTF baseColorFactor).
    i32 baseColorImage = -1; ///< sRGB colour + alpha.
    i32 normalImage = -1;    ///< Linear tangent-space normal map.
    f32 normalScale = 1.0f;  ///< Strength of the normal map (glTF normalTexture.scale).
    Vec3 emissive{0.0f};     ///< Linear factor (glTF emissiveFactor).
    i32 emissiveImage = -1;  ///< sRGB.
    AlphaMode alphaMode = AlphaMode::Opaque;
    f32 alphaCutoff = 0.5f; ///< Mask: fragments with alpha below are discarded.
    bool doubleSided = false;
};

/// Range of the index buffer drawn with one material.
struct Submesh
{
    u32 firstIndex = 0;
    u32 indexCount = 0;
    u32 material = 0;
    u32 lod = 0; ///< level of detail (asset.md "Detailstufen"): 0 = full, 1 and 2 coarser
};

/// Collision geometry of a model from its "COL_" nodes (contract: docs/modules/asset.md), in model
/// space like the vertices. COL_BOX_* arrives as the 8 corners of a Hull.
struct CollisionPart
{
    enum class Kind : u8
    {
        Hull, ///< convex hull of `points` (COL_HULL_*, COL_BOX_*); no indices
        Mesh, ///< triangles (any other COL_* node)
    };
    Kind kind = Kind::Mesh;
    std::vector<Vec3> points;
    std::vector<u32> indices;
};

/// Name prefixes of collision nodes (asset.md).
inline constexpr std::string_view kCollisionPrefix = "COL_";
/// Coarsest level of detail a static model may carry (lod0 .. lod2).
inline constexpr u32 kMaxLod = 2;
/// Nodes named "<name>_lod<n>" (n = 1, 2) are coarser levels of detail of a static model (asset.md
/// "Detailstufen (LOD) statischer Modelle"); "<name>" or "<name>_lod0" is level 0.
[[nodiscard]] bool isCoarserLod(std::string_view nodeName) noexcept;
/// The level a node belongs to: n for "<name>_lod<n>", 0 otherwise.
[[nodiscard]] u32 lodLevel(std::string_view nodeName) noexcept;
inline constexpr std::string_view kCollisionBoxPrefix = "COL_BOX_";
inline constexpr std::string_view kCollisionHullPrefix = "COL_HULL_";

/// CPU-side static mesh: node transforms baked in, one submesh per material.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    std::vector<Submesh> submeshes;
    std::vector<MaterialInfo> materials;
    std::vector<ImageSource> images;
    AABB bounds; ///< of the render vertices (collision parts not included)
    /// COL_ nodes: not drawn. Empty = the model has none and its render mesh collides.
    std::vector<CollisionPart> collision;
};

/// Highest LOD level among the submeshes (0: the model has no coarser levels).
[[nodiscard]] u32 maxLod(const MeshData& mesh) noexcept;

/// Loads the default scene of a glTF 2.0 file (.gltf with external or data: buffers, or .glb)
/// as one static mesh (ADR 0013). Coordinates need no conversion (+Y up, right-handed, metres).
/// Only triangle primitives are used; others are skipped with a warning. Nodes of coarser LODs (`_lod1`,
/// `_lod2`) become submeshes of their level (Submesh::lod); levels above kMaxLod are left out.
[[nodiscard]] Result<MeshData> loadGltf(const fs::Path& path);
/// Same, from memory; external buffers are resolved relative to `baseDirectory`. An empty
/// `baseDirectory` allows only self-contained data (GLB chunk, data: URIs); external buffers fail.
[[nodiscard]] Result<MeshData> loadGltf(std::span<const u8> bytes, const fs::Path& baseDirectory,
                                        std::string_view debugName = "<memory>");
} // namespace g7::asset

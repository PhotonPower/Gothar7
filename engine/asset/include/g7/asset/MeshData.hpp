#pragma once

#include <g7/core/FileSystem.hpp>
#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string>
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
};

/// CPU-side static mesh: node transforms baked in, one submesh per material.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    std::vector<Submesh> submeshes;
    std::vector<MaterialInfo> materials;
    std::vector<ImageSource> images;
    AABB bounds;
};

/// Loads the default scene of a glTF 2.0 file (.gltf with external or data: buffers, or .glb)
/// as one static mesh (ADR 0013). Coordinates need no conversion (+Y up, right-handed, metres).
/// Only triangle primitives are used; others are skipped with a warning.
[[nodiscard]] Result<MeshData> loadGltf(const fs::Path& path);
/// Same, from memory; external buffers are resolved relative to `baseDirectory`. An empty
/// `baseDirectory` allows only self-contained data (GLB chunk, data: URIs); external buffers fail.
[[nodiscard]] Result<MeshData> loadGltf(std::span<const u8> bytes, const fs::Path& baseDirectory,
                                        std::string_view debugName = "<memory>");
} // namespace g7::asset

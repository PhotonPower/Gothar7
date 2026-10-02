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
    Vec4 tangent{0.0f}; ///< xyz + handedness in w; all zero if the source had none.
};
static_assert(sizeof(Vertex) == 48);

/// Material parameters as far as known in M2 (the material model follows later).
struct MaterialInfo
{
    std::string name;
    Vec4 baseColor{1.0f};
    std::string baseColorTexture; ///< Image URI relative to the model file; empty if none/embedded.
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
    AABB bounds;
};

/// Loads the default scene of a glTF 2.0 file (.gltf with external or data: buffers, or .glb)
/// as one static mesh (ADR 0013). Coordinates need no conversion (+Y up, right-handed, metres).
/// Only triangle primitives are used; others are skipped with a warning.
[[nodiscard]] Result<MeshData> loadGltf(const fs::Path& path);
/// Same, from memory; external buffers are resolved relative to `baseDirectory`.
[[nodiscard]] Result<MeshData> loadGltf(std::span<const u8> bytes, const fs::Path& baseDirectory,
                                        std::string_view debugName = "<memory>");
} // namespace g7::asset

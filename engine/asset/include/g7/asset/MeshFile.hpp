#pragma once

#include <g7/asset/MeshData.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// Cooked static mesh, ".g7mesh" version 2 (ADR 0016), little-endian:
///
///     Header  magic "G7MS", version u32, vertexCount u32, indexCount u32, submeshCount u32,
///             materialCount u32, imageCount u32, collisionCount u32, bounds min/max (6 x f32)
///     Vertices   position, normal, uv, tangent (12 x f32 each, the asset::Vertex layout)
///     Indices    u32
///     Submeshes  firstIndex, indexCount, material (u32 each)
///     Materials  name (u16 length + UTF-8), baseColor 4 x f32, baseColorImage i32, normalImage i32,
///                normalScale f32, emissive 3 x f32, emissiveImage i32, alphaMode u8,
///                alphaCutoff f32, doubleSided u8
///     Images     uri (u16 + UTF-8), mimeType (u16 + UTF-8), encoded bytes (u32 length + data)
///     Collision  kind u8 (0 hull, 1 mesh), pointCount u32, indexCount u32, points (3 x f32),
///                indices u32  (version 2; version 1 had "reserved = 0" there and no collision)
///
/// In meshes written by g7-cook, image uris are VFS paths from the archive root (the cooker resolves
/// "../textures/x.png" once, since the VFS rejects ".."), and encoded bytes are empty.
inline constexpr char kMeshMagic[4] = {'G', '7', 'M', 'S'};
inline constexpr u32 kMeshVersion = 3; // 3: LOD level per submesh
/// Oldest version still read (no collision parts).
inline constexpr u32 kMeshMinVersion = 1;

[[nodiscard]] std::vector<u8> serializeMesh(const MeshData& mesh);
/// Parses and validates (counts, index and submesh ranges, material and image references).
[[nodiscard]] Result<MeshData> deserializeMesh(std::span<const u8> bytes,
                                               std::string_view debugName = "<memory>");
} // namespace g7::asset

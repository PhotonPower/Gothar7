#pragma once

// Cooked skinned model, ".g7skin" (ADR 0025, plan M17 cooked figures part B): SkinnedModelData as it comes
// from loadSkinnedGltf, without a glTF parser at load time.

#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// ".g7skin" version 1, little-endian; strings are u16 length + UTF-8, vectors 3/4 x f32, matrices 16 x f32
/// (column-major), quaternions x y z w:
///
///     Header     magic "G7SK", version u32
///     Skeleton   boneCount u32; per bone: name, parent i32, translation, rotation, scale; rootParent
///     InverseBind  boneCount matrices
///     Bounds     min, max
///     Materials  count u32; per material the .g7mesh record (version 4: wind included)
///     Images     count u32; uri, mimeType, encoded bytes (u32 length + data)
///     Parts      count u32; per part: node, role, lod u32, vertexCount u32, indexCount u32,
///                submeshCount u32, morphCount u32, primitiveCount u32; vertices (asset::Vertex, 12 x f32),
///                joints (4 x u16), weights (4 x f32), indices u32, submeshes (4 x u32),
///                morphs (name, hasNormals u8, positions, normals if any), primitives (5 x u32)
///     Assembly   version u32, part; neck: count u32, per LOD node: name, ringCount u32, per ring point:
///                count u32 + [primitive, vertex] u32 pairs; falloff: count u32, per LOD node: name, count
///                u32, [primitive, vertex, ringPoint u32, weight f64]; coversBody; covers: count u32, per LOD
///                node: name, count u32, [primitive, first, end] u32; hides: count u32, names
///
/// Vertex and triangle order stay as loaded from glTF: the assembly data points into them by
/// [glTF primitive, vertex]. Image uris stay as in the source (relative to the part), like glTF.
inline constexpr char kSkinMagic[4] = {'G', '7', 'S', 'K'};
inline constexpr u32 kSkinVersion = 1;

[[nodiscard]] std::vector<u8> serializeSkin(const SkinnedModelData& model);
/// Parses and validates: counts against the data, bone parents before children, joints < bone count,
/// indices < vertex count, submesh and primitive ranges, material and image references, morph sizes.
[[nodiscard]] Result<SkinnedModelData> deserializeSkin(std::span<const u8> bytes,
                                                       std::string_view debugName = "<memory>");
} // namespace g7::asset

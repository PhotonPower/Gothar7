#pragma once

// Internal: glTF helpers shared by the static mesh loader (Gltf.cpp) and the skinned model and animation
// loader (SkinnedGltf.cpp). fastgltf stays private to the asset module.

#include <g7/asset/MeshData.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>

#include <fastgltf/core.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace g7::asset::gltf
{
[[nodiscard]] Mat4 toGlm(const fastgltf::math::fmat4x4& matrix);
[[nodiscard]] std::vector<MaterialInfo> readMaterials(const fastgltf::Asset& asset);
[[nodiscard]] std::vector<ImageSource> readImages(const fastgltf::Asset& asset, std::string_view debugName);
/// Tangents and flat normals for vertices[firstVertex..] from triangle `indices` (absolute).
void computeTangents(std::vector<Vertex>& vertices, std::span<const u32> indices, usize firstVertex);
void computeFlatNormals(std::vector<Vertex>& vertices, std::span<const u32> indices, usize firstVertex);
/// Parses glTF/GLB data; an empty `baseDirectory` allows only self-contained data.
[[nodiscard]] Result<fastgltf::Asset> parseAsset(fastgltf::GltfDataBuffer& data,
                                                 const fs::Path& baseDirectory, std::string_view debugName);
} // namespace g7::asset::gltf

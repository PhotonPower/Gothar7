#pragma once

// Internal: extras of glTF files that fastgltf only hands over as simdjson objects, read from the JSON text
// with nlohmann-json (private, ADR 0017).

#include <g7/asset/MeshData.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// The JSON text of a .gltf (all of it) or .glb (chunk 0); empty if a .glb has no JSON chunk.
[[nodiscard]] std::string_view gltfJsonText(std::span<const u8> bytes) noexcept;
/// M17 W: `extras.wind`, `extras.wind_strength` and `extras.wind_pivot` of the file's materials into
/// `materials` (same order as the file's). Unknown or malformed values are warned about and leave the
/// material still (a model must not fail to load over its wind).
void readMaterialWind(std::span<const u8> bytes, std::vector<MaterialInfo>& materials,
                      std::string_view debugName);
} // namespace g7::asset

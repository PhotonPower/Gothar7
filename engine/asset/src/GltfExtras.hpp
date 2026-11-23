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
/// The extras of the file's materials into `materials` (same order as the file's): M17 W `extras.wind`,
/// `extras.wind_strength` and `extras.wind_pivot`; M17 windows F1 `extras.glass`, `extras.glass_tint` and
/// `extras.glass_pattern`. Unknown or malformed values are warned about and fall back to the defaults (still,
/// no glass, the default tint or pattern): a model must not fail to load over its extras.
void readMaterialExtras(std::span<const u8> bytes, std::vector<MaterialInfo>& materials,
                        std::string_view debugName);
} // namespace g7::asset

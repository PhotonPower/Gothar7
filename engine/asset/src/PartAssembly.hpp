#pragma once

// Internal: reading the assembly data of figure parts (asset.extras.gothar, §6.2).

#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/Result.hpp>

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// The part data of a .glb/.gltf; version 0 (empty) when the file has none.
[[nodiscard]] Result<SkinnedModelData::Assembly> readPartAssembly(std::span<const u8> bytes,
                                                                  std::string_view debugName);
/// The morph target names per skinned node (glTF mesh extras.targetNames, the Blender export), by node name.
[[nodiscard]] std::map<std::string, std::vector<std::string>> readMorphNames(std::span<const u8> bytes);
} // namespace g7::asset

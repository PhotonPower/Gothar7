#pragma once

// Internal: reading the assembly data of figure parts (asset.extras.gothar, §6.2).

#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/Result.hpp>

#include <span>
#include <string_view>

namespace g7::asset
{
/// The part data of a .glb/.gltf; version 0 (empty) when the file has none.
[[nodiscard]] Result<SkinnedModelData::Assembly> readPartAssembly(std::span<const u8> bytes,
                                                                  std::string_view debugName);
} // namespace g7::asset

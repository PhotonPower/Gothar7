#pragma once

// Module: g7::asset
// Virtuelles Dateisystem, Archive, Asset-Handles, asynchrones Laden, Hot-Reload.
// Spezifikation: docs/modules/asset.md   |   Roadmap: M3
//
// Status: Bilder, glTF, prozedurale Meshes (M2); VFS und .g7pak (M3). Handles folgen.

#include <string_view>

namespace g7::asset
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::asset

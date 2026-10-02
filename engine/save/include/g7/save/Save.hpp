#pragma once

// Module: g7::save
// Serialisierung des Spielzustands, Versionierung, Quicksave.
// Spezifikation: docs/modules/save.md   |   Roadmap: M15
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::save
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::save

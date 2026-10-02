#pragma once

// Module: g7::platform
// Fenster, Eingabe, Dateisystem-Pfade, OS-Abstraktion (SDL3).
// Spezifikation: docs/modules/platform.md   |   Roadmap: M1
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::platform
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::platform

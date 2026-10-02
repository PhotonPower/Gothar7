#pragma once

// Module: g7::platform
// Fenster, Eingabe, Dateisystem-Pfade, OS-Abstraktion (SDL3).
// Spezifikation: docs/modules/platform.md   |   Roadmap: M1
//
// Public API: Window.hpp (window, events), Paths.hpp (user data directory); Input follows in M1.

#include <string_view>

namespace g7::platform
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::platform

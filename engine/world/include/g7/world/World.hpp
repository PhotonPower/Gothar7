#pragma once

// Module: g7::world
// Szene (EnTT), Vobs, Weltformat, Tag/Nacht, Wetter, Wegnetz-Daten.
// Spezifikation: docs/modules/world.md   |   Roadmap: M4
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::world
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::world

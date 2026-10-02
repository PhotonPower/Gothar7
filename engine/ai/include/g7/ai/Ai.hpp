#pragma once

// Module: g7::ai
// Pfadsuche auf dem Wegnetz, Wahrnehmung, NPC-Zustandsautomat, Tagesablaeufe.
// Spezifikation: docs/modules/ai.md   |   Roadmap: M9
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::ai
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::ai

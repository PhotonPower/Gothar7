#pragma once

// Module: g7::ui
// Spiel-UI (HUD, Menues, Inventar, Dialogauswahl), Lokalisierung, Debug-UI (ImGui).
// Spezifikation: docs/modules/ui.md   |   Roadmap: M14
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::ui
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::ui

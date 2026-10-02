#pragma once

// Module: g7::gameplay
// Attribute, Gilden, Inventar, Items, Interaktion, Dialoge, Quests, Kampf, Magie.
// Spezifikation: docs/modules/gameplay.md   |   Roadmap: M8
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::gameplay
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::gameplay

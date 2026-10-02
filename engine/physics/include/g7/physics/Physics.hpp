#pragma once

// Module: g7::physics
// Kollision, Raycasts, Charakter-Controller, Trigger (Jolt Physics).
// Spezifikation: docs/modules/physics.md   |   Roadmap: M5
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::physics
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::physics

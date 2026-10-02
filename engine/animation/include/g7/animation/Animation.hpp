#pragma once

// Module: g7::animation
// Skelettanimation, Blending, Layer, Zustandsautomaten, Events, Morph-Targets.
// Spezifikation: docs/modules/animation.md   |   Roadmap: M6
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::animation
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::animation

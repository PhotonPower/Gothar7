#pragma once

// Module: g7::audio
// 3D-Sound, Ambient-Zonen, Sprachausgabe, dynamisches Musiksystem (miniaudio).
// Spezifikation: docs/modules/audio.md   |   Roadmap: M13
//
// Status: Platzhalter. Die oeffentliche API wird in der genannten Phase entworfen.

#include <string_view>

namespace g7::audio
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::audio

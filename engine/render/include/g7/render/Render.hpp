#pragma once

// Module: g7::render
// OpenGL-4.6-Renderer: RHI, Materialien, Meshes, Licht, Schatten, Himmel, Debug-Draw.
// Spezifikation: docs/modules/render.md   |   Roadmap: M2
//
// Public API so far: Device.hpp (RHI root: GL loading, debug output), DebugOutput.hpp.

#include <string_view>

namespace g7::render
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::render

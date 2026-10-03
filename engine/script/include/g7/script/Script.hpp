#pragma once

// Module: g7::script
// Lua-5.4-Skript-VM (sol2), Bindings, Hot-Reload, Konsole.
// Spezifikation: docs/modules/script.md   |   Roadmap: M7
//
// Die VM: ScriptVm.hpp, Werte: Value.hpp (M7 Teil A).

#include <string_view>

namespace g7::script
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::script

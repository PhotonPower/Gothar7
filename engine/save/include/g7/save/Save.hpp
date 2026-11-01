#pragma once

// Module: g7::save - save games (docs/modules/save.md, M15). The game state as data and JSON: GameState.hpp;
// the world delta: WorldDelta.hpp.

#include <string_view>

namespace g7::save
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;
} // namespace g7::save

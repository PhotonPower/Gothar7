#pragma once

// Content defined in Lua (M7 part C): the instance kinds Item, Npc, Info, Quest and Routine with the fields
// the engine knows so far (docs/modules/script.md). Gameplay owns them; M8 and later add fields (inventory,
// attributes, dialogs) - unknown fields are errors, so typos show at load time.

#include <g7/script/ScriptVm.hpp>

namespace g7::gameplay
{
/// Declares Item, Npc, Info, Quest and Routine in `vm` (before ScriptVm::loadAll).
void defineContentKinds(script::ScriptVm& vm);
} // namespace g7::gameplay

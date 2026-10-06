// The hero's fighting keys (M11 part B, owner decision K1): Gothic 1 - the action key held plus a direction
// -, the mouse as second assignment. Pure, for tests: what one frame's input asks for.
#pragma once

#include <g7/gameplay/Combat.hpp>
#include <g7/platform/Actions.hpp>

#include <optional>
#include <string>

namespace g7::runtime
{
struct CombatInput
{
    /// "attack", "parry" or "dodge" with the kind of attack; empty: nothing asked for.
    std::optional<std::pair<std::string, gameplay::AttackKind>> move;
    /// The action key is held: the hero does not walk (Gothic 1).
    bool holdsAction = false;
};

/// With the action key held: forward strikes (again within the combo window: the next hit), left/right swing
/// to the side, back parries, jump steps back. Mouse: left strikes (to the side with a left/right key held),
/// right parries.
[[nodiscard]] CombatInput combatInput(const platform::ActionMap& actions, const platform::Input& input);
} // namespace g7::runtime

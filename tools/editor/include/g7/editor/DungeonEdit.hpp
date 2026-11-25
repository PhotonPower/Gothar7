#pragma once

// Dungeon parts in the editor (dungeon D7, docs/modules/dungeon.md "Editor"): the wiring of senders
// (triggers, plates, teleporters, levers, buttons, chests and relays) to their targets as lines, and the
// checks of a world's wiring - tested without a window.

#include <g7/core/Math.hpp>
#include <g7/editor/TriggerEdit.hpp>
#include <g7/world/Components.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::world
{
class Scene;
}

namespace g7::editor
{
/// Names from one text: separated by commas or semicolons, spaces around them dropped, empty ones left out.
[[nodiscard]] std::vector<std::string> splitNames(std::string_view text);
/// The names as one text "A, B" (what splitNames reads back).
[[nodiscard]] std::string joinNames(std::span<const std::string> names);

/// One line of the wiring: from a sender to a vob it names.
struct DungeonLink
{
    enum class Kind : u8
    {
        Event,    ///< `targets` of a trigger, mob or relay
        Teleport, ///< a teleporter to its `to`
    };
    Kind kind = Kind::Event;
    world::VobId from;
    world::VobId to; ///< invalid: no vob of that name (the check reports it)
    std::string target;
    std::string action;
    Vec3 fromPosition{0.0f};
    Vec3 toPosition{0.0f};
};

/// Every line of the scene's wiring, by sender id, then in the order of its targets.
[[nodiscard]] std::vector<DungeonLink> dungeonLinks(const world::Scene& scene);

/// The colour of a line by its action: open/on green, close/off red, toggle/trigger yellow, wake orange,
/// lock/unlock blue, a teleporter violet.
[[nodiscard]] Vec4 linkColor(const DungeonLink& link) noexcept;

/// The issues of the scene's dungeon parts, by vob id (TriggerIssue as for the triggers):
/// - a target naming no vob, or a name shared by several vobs (names are unique per world, the contract);
/// - a plate or relay without targets;
/// - a teleporter whose `to` is no vob, whose world does not exist or lacks the start point (`starts`);
/// - relays on a cycle;
/// - a spawn marker of an NPC instance the scripts do not know (`npcs` null: not checked);
/// - a moving part without a name (nothing can reach it).
[[nodiscard]] std::vector<TriggerIssue> checkDungeon(const world::Scene& scene, const StartsFn& starts,
                                                     const std::vector<std::string>* npcs);
} // namespace g7::editor

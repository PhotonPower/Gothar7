#pragma once

// Trigger editing (M16 part C, docs/modules/editor.md): the choice lists of the inspector (script functions,
// worlds, their start points) and the checks of a world's triggers, tested without a window.

#include <g7/world/WorldFile.hpp>

#include <functional>
#include <optional>
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
/// The entry of a choice list meaning "none" (an empty text in the component).
inline constexpr std::string_view kNoChoice = "-";

/// A choice field's entries: "-" first, then `known`; `current` is added if it is not among them (so an
/// unknown name stays visible - the check reports it). `index` points at `current` ("-" for empty).
struct ChoiceList
{
    std::vector<std::string> items;
    int index = 0;
};
[[nodiscard]] ChoiceList choiceList(std::string_view current, std::span<const std::string> known);
/// The component text of entry `index` ("-" and out of range: empty).
[[nodiscard]] std::string choiceValue(std::span<const std::string> items, int index);

/// The names of a world's start points (vobs of type start), in file order.
[[nodiscard]] std::vector<std::string> startNames(const world::WorldFile& world);

struct TriggerIssue
{
    world::VobId id;
    std::string name;
    std::string text;
};

/// The start points of a world (VFS path); nullopt if there is no such world.
using StartsFn = std::function<std::optional<std::vector<std::string>>(std::string_view world)>;

/// The issues of the scene's triggers, by vob id: onEnter/onLeave naming no script function (`functions`
/// null: not checked, e.g. without scripts), a level change to a world that does not exist or to a start
/// point the world lacks (names compared ignoring case, as the engine does), a start point without a world,
/// and a trigger that does nothing (no function, no level change, not a private area).
[[nodiscard]] std::vector<TriggerIssue>
checkTriggers(const world::Scene& scene, const std::vector<std::string>* functions, const StartsFn& starts);
} // namespace g7::editor

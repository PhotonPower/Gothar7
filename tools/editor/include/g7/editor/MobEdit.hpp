#pragma once

// Mob editing (M16 part D, docs/modules/editor.md): a mob vob names a Lua `Mob` definition or a bare type of
// data/mobs.toml (gameplay.md); the editor offers both as a list, shows what the definition holds (owner,
// lock, key, contents - content stays in Lua) and checks it against the lists of NPCs, guilds and items.

#include <g7/runtime/Engine.hpp>

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
/// Every name a mob vob may carry: the Lua definitions, then the bare types not used as a definition name;
/// sorted.
[[nodiscard]] std::vector<std::string> mobDefinitionChoices(std::span<const MobDefinitionInfo> definitions,
                                                            std::span<const std::string> types);

/// What a vob's `definition` means: its Lua definition, or a bare type (unlocked, empty, named after it, as
/// the engine does); nullopt if it is neither.
[[nodiscard]] std::optional<MobDefinitionInfo> resolveMob(std::span<const MobDefinitionInfo> definitions,
                                                          std::span<const std::string> types,
                                                          std::string_view definition);

/// The lines the inspector shows for a definition ("type chest", "owner npc_x", "contents it_apple x3, ...").
[[nodiscard]] std::vector<std::pair<std::string, std::string>> mobSummary(const MobDefinitionInfo& mob);

/// Lists to check against (sorted).
struct MobLists
{
    std::vector<MobDefinitionInfo> definitions;
    std::vector<std::string> types;
    std::vector<std::string> npcs;
    std::vector<std::string> guilds;
    std::vector<std::string> items;
};

struct MobIssue
{
    world::VobId id;
    std::string name;
    std::string text;
};

/// The issues of the scene's mob vobs, by id: an unknown definition, a type missing from mobs.toml, an owner
/// that is neither NPC nor guild, a key or contents that are no items, a lock of other letters than L and R,
/// and a vob stored open that is no door.
[[nodiscard]] std::vector<MobIssue> checkMobs(const world::Scene& scene, const MobLists& lists);
} // namespace g7::editor

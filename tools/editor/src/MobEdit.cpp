#include <g7/editor/MobEdit.hpp>
#include <g7/world/Scene.hpp>

#include <algorithm>
#include <format>

namespace g7::editor
{
namespace
{
bool listed(std::span<const std::string> sorted, std::string_view name)
{
    return std::ranges::binary_search(sorted, name, std::less<>{});
}
} // namespace

std::vector<std::string> mobDefinitionChoices(std::span<const MobDefinitionInfo> definitions,
                                              std::span<const std::string> types)
{
    std::vector<std::string> names;
    for (const MobDefinitionInfo& d : definitions)
    {
        names.push_back(d.definition);
    }
    names.insert(names.end(), types.begin(), types.end());
    std::ranges::sort(names);
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::optional<MobDefinitionInfo> resolveMob(std::span<const MobDefinitionInfo> definitions,
                                            std::span<const std::string> types, std::string_view definition)
{
    if (const auto it = std::ranges::find(definitions, definition, &MobDefinitionInfo::definition);
        it != definitions.end())
    {
        return *it;
    }
    if (std::ranges::find(types, definition) != types.end())
    {
        MobDefinitionInfo bare; // a bare type: unlocked, empty, named after the type
        bare.definition = std::string(definition);
        bare.name = bare.definition;
        bare.type = bare.definition;
        return bare;
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> mobSummary(const MobDefinitionInfo& mob)
{
    std::string contents;
    for (const auto& [item, count] : mob.contents)
    {
        contents += std::format("{}{}{}", contents.empty() ? "" : ", ", item,
                                count > 1 ? std::format(" x{}", count) : "");
    }
    const auto orDash = [](const std::string& s) { return s.empty() ? std::string("-") : s; };
    return {{"mob type", orDash(mob.type)},   {"mob name", orDash(mob.name)},
            {"mob owner", orDash(mob.owner)}, {"mob lock", orDash(mob.lock)},
            {"mob key", orDash(mob.key)},     {"mob contents", orDash(contents)}};
}

std::vector<MobIssue> checkMobs(const world::Scene& scene, const MobLists& lists)
{
    std::vector<MobIssue> issues;
    scene.each<world::Vob, world::MobRef>(
        [&](entt::entity, const world::Vob& vob, const world::MobRef& ref)
        {
            const auto add = [&](std::string text)
            { issues.push_back({vob.id, vob.nameText, std::move(text)}); };
            const auto mob = resolveMob(lists.definitions, lists.types, ref.definition);
            if (!mob)
            {
                add(std::format("'{}' is neither a Mob definition nor a mob type", ref.definition));
                return;
            }
            if (!listed(lists.types, mob->type))
            {
                add(std::format("type '{}' is not in data/mobs.toml", mob->type));
            }
            if (!mob->owner.empty() && !listed(lists.npcs, mob->owner) && !listed(lists.guilds, mob->owner))
            {
                add(std::format("owner '{}' is neither an NPC nor a guild", mob->owner));
            }
            if (!mob->key.empty() && !listed(lists.items, mob->key))
            {
                add(std::format("key '{}' is no item", mob->key));
            }
            for (const auto& [item, count] : mob->contents)
            {
                if (!listed(lists.items, item))
                {
                    add(std::format("contents: '{}' is no item", item));
                }
            }
            if (!std::ranges::all_of(mob->lock, [](char c) { return c == 'L' || c == 'R'; }))
            {
                add(std::format("lock '{}': L and R only", mob->lock));
            }
            if (ref.open && mob->type != "door")
            {
                add("stored open, but only doors open");
            }
        });
    std::ranges::stable_sort(issues, {}, [](const MobIssue& i) { return i.id.value; });
    return issues;
}
} // namespace g7::editor

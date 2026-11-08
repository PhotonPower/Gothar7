#include <g7/core/StringUtil.hpp>
#include <g7/editor/TriggerEdit.hpp>
#include <g7/world/Scene.hpp>

#include <algorithm>
#include <format>

namespace g7::editor
{
ChoiceList choiceList(std::string_view current, std::span<const std::string> known)
{
    ChoiceList list;
    list.items.emplace_back(kNoChoice);
    list.items.insert(list.items.end(), known.begin(), known.end());
    if (current.empty())
    {
        return list;
    }
    const auto at = std::ranges::find(list.items, current);
    if (at != list.items.end())
    {
        list.index = static_cast<int>(at - list.items.begin());
        return list;
    }
    list.items.emplace_back(current); // unknown: kept and shown (the check reports it)
    list.index = static_cast<int>(list.items.size() - 1);
    return list;
}

std::string choiceValue(std::span<const std::string> items, int index)
{
    if (index <= 0 || static_cast<usize>(index) >= items.size() ||
        items[static_cast<usize>(index)] == kNoChoice)
    {
        return {};
    }
    return items[static_cast<usize>(index)];
}

std::vector<std::string> startNames(const world::WorldFile& world)
{
    std::vector<std::string> names;
    for (const world::WorldFileVob& v : world.vobs)
    {
        if (v.type == world::VobType::Start)
        {
            names.push_back(v.name);
        }
    }
    return names;
}

std::vector<TriggerIssue> checkTriggers(const world::Scene& scene, const std::vector<std::string>* functions,
                                        const StartsFn& starts)
{
    std::vector<TriggerIssue> issues;
    scene.each<world::Vob, world::TriggerVolume>(
        [&](entt::entity, const world::Vob& vob, const world::TriggerVolume& t)
        {
            const auto add = [&](std::string text)
            { issues.push_back({vob.id, vob.nameText, std::move(text)}); };
            for (const auto& [what, function] :
                 {std::pair{"onEnter", &t.onEnter}, std::pair{"onLeave", &t.onLeave}})
            {
                if (functions != nullptr && !function->empty() &&
                    !std::ranges::binary_search(*functions, *function))
                {
                    add(std::format("{}: no script function '{}'", what, *function));
                }
            }
            if (!t.changeWorld.empty())
            {
                const auto names = starts ? starts(t.changeWorld) : std::nullopt;
                if (!names)
                {
                    add(std::format("level change: no world '{}'", t.changeWorld));
                }
                else if (!t.changeStart.empty() &&
                         std::ranges::none_of(*names, [&](const std::string& n)
                                              { return equalsIgnoreCase(n, t.changeStart); }))
                {
                    add(std::format("level change: '{}' has no start point '{}'", t.changeWorld,
                                    t.changeStart));
                }
                else if (names->empty())
                {
                    add(std::format("level change: '{}' has no start point", t.changeWorld));
                }
            }
            else if (!t.changeStart.empty())
            {
                add(std::format("start point '{}' without a world to change to", t.changeStart));
            }
            if (t.onEnter.empty() && t.onLeave.empty() && t.changeWorld.empty() && t.owner.empty())
            {
                add("does nothing (no function, no level change, no owner)");
            }
        });
    std::ranges::stable_sort(issues, {}, [](const TriggerIssue& i) { return i.id.value; });
    return issues;
}
} // namespace g7::editor

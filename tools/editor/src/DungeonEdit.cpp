#include <g7/core/StringUtil.hpp>
#include <g7/editor/DungeonEdit.hpp>
#include <g7/world/EventNet.hpp>
#include <g7/world/Scene.hpp>

#include <algorithm>
#include <format>
#include <map>

namespace g7::editor
{
namespace
{
/// The world vobs by name (runtime vobs left out): how many carry it and the first one.
struct NameIndex
{
    struct Entry
    {
        usize count = 0;
        entt::entity vob = entt::null;
    };
    std::map<std::string, Entry, std::less<>> names;

    explicit NameIndex(const world::Scene& scene)
    {
        scene.each<world::Vob>(
            [&](entt::entity e, const world::Vob& vob)
            {
                if (vob.id.runtime() || vob.nameText.empty())
                {
                    return;
                }
                Entry& entry = names[vob.nameText];
                if (entry.count++ == 0 || vob.id.value < scene.idOf(entry.vob).value)
                {
                    entry.vob = e;
                }
            });
    }

    [[nodiscard]] const Entry* find(std::string_view name) const
    {
        const auto it = names.find(name);
        return it != names.end() ? &it->second : nullptr;
    }
};

/// A sender's targets and action, or null if it sends nothing.
struct Sending
{
    const std::vector<std::string>* targets = nullptr;
    std::string_view action;
};

Sending sendingOf(const world::Scene& scene, entt::entity e)
{
    if (const auto* t = scene.get<world::TriggerVolume>(e); t && !t->targets.empty())
    {
        return {&t->targets, t->action};
    }
    if (const auto* m = scene.get<world::MobRef>(e); m && !m->targets.empty())
    {
        return {&m->targets, m->action};
    }
    if (const auto* r = scene.get<world::Relay>(e); r && !r->targets.empty())
    {
        return {&r->targets, r->action};
    }
    return {};
}

Vec3 positionOf(const world::Scene& scene, entt::entity e)
{
    return Vec3(scene.worldMatrix(e)[3]);
}
} // namespace

std::vector<std::string> splitNames(std::string_view text)
{
    std::vector<std::string> names;
    usize start = 0;
    while (start <= text.size())
    {
        const usize end = std::min(text.find_first_of(",;", start), text.size());
        std::string_view part = text.substr(start, end - start);
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t'))
        {
            part.remove_prefix(1);
        }
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t'))
        {
            part.remove_suffix(1);
        }
        if (!part.empty())
        {
            names.emplace_back(part);
        }
        start = end + 1;
    }
    return names;
}

std::string joinNames(std::span<const std::string> names)
{
    std::string text;
    for (const std::string& name : names)
    {
        text += (text.empty() ? "" : ", ") + name;
    }
    return text;
}

std::vector<DungeonLink> dungeonLinks(const world::Scene& scene)
{
    const NameIndex index(scene);
    std::vector<DungeonLink> links;
    scene.each<world::Vob>(
        [&](entt::entity e, const world::Vob& vob)
        {
            if (vob.id.runtime())
            {
                return;
            }
            const auto add = [&](DungeonLink::Kind kind, const std::string& target, std::string_view action)
            {
                DungeonLink link{kind, vob.id, {}, target, std::string(action), positionOf(scene, e), {}};
                if (const NameIndex::Entry* found = index.find(target))
                {
                    link.to = scene.idOf(found->vob);
                    link.toPosition = positionOf(scene, found->vob);
                }
                links.push_back(std::move(link));
            };
            if (const Sending s = sendingOf(scene, e); s.targets != nullptr)
            {
                for (const std::string& target : *s.targets)
                {
                    add(DungeonLink::Kind::Event, target, s.action);
                }
            }
            if (const auto* t = scene.get<world::TriggerVolume>(e);
                t && t->teleport && !t->teleport->to.empty())
            {
                add(DungeonLink::Kind::Teleport, t->teleport->to, "teleport");
            }
        });
    std::ranges::stable_sort(links, {}, [](const DungeonLink& l) { return l.from.value; });
    return links;
}

Vec4 linkColor(const DungeonLink& link) noexcept
{
    if (link.kind == DungeonLink::Kind::Teleport)
    {
        return {0.7f, 0.4f, 1.0f, 1.0f};
    }
    const std::string_view a = link.action;
    if (a == "open" || a == "on")
    {
        return {0.3f, 1.0f, 0.3f, 1.0f};
    }
    if (a == "close" || a == "off")
    {
        return {1.0f, 0.3f, 0.3f, 1.0f};
    }
    if (a == "wake")
    {
        return {1.0f, 0.6f, 0.1f, 1.0f};
    }
    if (a == "lock" || a == "unlock")
    {
        return {0.3f, 0.6f, 1.0f, 1.0f};
    }
    return {1.0f, 0.95f, 0.3f, 1.0f}; // toggle, trigger
}

std::vector<TriggerIssue> checkDungeon(const world::Scene& scene, const StartsFn& starts,
                                       const std::vector<std::string>* npcs)
{
    const NameIndex index(scene);
    std::vector<TriggerIssue> issues;
    world::EventNet net; // only for its cycle search
    scene.each<world::Vob>(
        [&](entt::entity e, const world::Vob& vob)
        {
            if (vob.id.runtime())
            {
                return;
            }
            const auto add = [&](std::string text)
            { issues.push_back({vob.id, vob.nameText, std::move(text)}); };
            const Sending s = sendingOf(scene, e);
            if (s.targets != nullptr)
            {
                for (const std::string& target : *s.targets)
                {
                    const NameIndex::Entry* found = index.find(target);
                    if (found == nullptr)
                    {
                        add(std::format("target '{}': no vob of that name", target));
                    }
                    else if (found->count > 1)
                    {
                        add(std::format("target '{}': {} vobs carry that name (names are unique per world)",
                                        target, found->count));
                    }
                }
                net.addSender(vob.nameText, *s.targets);
            }
            if (const auto* relay = scene.get<world::Relay>(e))
            {
                net.addRelay(vob.nameText, *relay);
                if (relay->targets.empty())
                {
                    add("relay without targets");
                }
            }
            if (const auto* t = scene.get<world::TriggerVolume>(e))
            {
                if (t->plate && t->targets.empty() && t->targetId.value == 0)
                {
                    add("plate without targets");
                }
                if (t->teleport)
                {
                    const world::Teleport& p = *t->teleport;
                    if (!p.to.empty() && index.find(p.to) == nullptr)
                    {
                        add(std::format("teleporter: no vob '{}' to go to", p.to));
                    }
                    else if (p.to.empty() && p.world.empty())
                    {
                        add("teleporter without a target");
                    }
                    else if (!p.world.empty())
                    {
                        const auto names = starts ? starts(p.world) : std::nullopt;
                        if (!names)
                        {
                            add(std::format("teleporter: no world '{}'", p.world));
                        }
                        else if (std::ranges::none_of(*names, [&](const std::string& n)
                                                      { return equalsIgnoreCase(n, p.start); }))
                        {
                            add(std::format("teleporter: '{}' has no start point '{}'", p.world, p.start));
                        }
                    }
                }
            }
            if (const auto* spawn = scene.get<world::Spawn>(e);
                spawn && npcs != nullptr && !std::ranges::binary_search(*npcs, spawn->npc))
            {
                add(std::format("spawn: no NPC instance '{}'", spawn->npc));
            }
            if (scene.get<world::Mover>(e) != nullptr && vob.nameText.empty())
            {
                add("moving part without a name: no event can reach it");
            }
        });
    for (const std::string& relay : net.cycles())
    {
        if (const NameIndex::Entry* found = index.find(relay))
        {
            issues.push_back(
                {scene.idOf(found->vob), relay, "relay on a cycle (it fires at most once per step)"});
        }
    }
    std::ranges::stable_sort(issues, {}, [](const TriggerIssue& i) { return i.id.value; });
    return issues;
}
} // namespace g7::editor

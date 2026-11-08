#include <g7/editor/Commands.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/editor/Prefabs.hpp>
#include <g7/editor/WaynetEdit.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldScene.hpp>

#include <algorithm>
#include <format>
#include <map>

namespace g7::editor
{
namespace
{
/// "FP_SIT_CAMPFIRE_01" -> "FP_SIT_"; "WP_CAMP_03" -> "WP_".
std::string prefixOf(std::string_view name)
{
    if (name.starts_with("FP_"))
    {
        const usize type = name.find('_', 3);
        return std::string(type == std::string_view::npos ? name : name.substr(0, type + 1));
    }
    return "WP_";
}

bool near(const Vec3& a, const Vec3& b, f32 reach)
{
    return glm::length(Vec2(a.x - b.x, a.z - b.z)) <= reach;
}
} // namespace

Result<world::WorldFile> makePrefab(const world::Scene& scene, std::span<const world::VobId> roots,
                                    const world::WaynetData& waynet, std::string name, f32 reach)
{
    if (roots.empty())
    {
        return Error{"a prefab needs at least one vob"};
    }
    world::WorldFile prefab;
    prefab.name = std::move(name);
    // The roots' world placement relative to the first root (translation only).
    const entt::entity first = scene.findById(roots.front());
    if (!scene.valid(first))
    {
        return Error{std::format("no vob {}", roots.front().value)};
    }
    const Vec3 origin = worldTransformOf(scene, first).position;
    std::map<u64, u64> ids; // scene id -> template id
    std::vector<Vec3> positions;
    for (const VobState& state : captureVobs(scene, roots)) // parents before children
    {
        if (!state.json || ids.contains(state.id.value))
        {
            continue;
        }
        auto vob = world::parseVobJson(*state.json, "prefab");
        if (!vob)
        {
            return vob.error();
        }
        world::WorldFileVob v = std::move(vob).value();
        const entt::entity e = scene.findById(state.id);
        positions.push_back(worldTransformOf(scene, e).position);
        const bool root = std::ranges::find(roots, state.id) != roots.end() || !ids.contains(v.parent.value);
        if (root)
        {
            Transform world = worldTransformOf(scene, e);
            world.position -= origin;
            v.transform = world;
            v.parent = {};
        }
        else
        {
            v.parent = world::VobId{ids.at(v.parent.value)};
        }
        ids[state.id.value] = ids.size() + 1;
        v.id = world::VobId{ids[state.id.value]};
        prefab.vobs.push_back(std::move(v));
    }
    if (prefab.vobs.size() < roots.size())
    {
        return Error{"unknown vobs in the selection"};
    }
    prefab.nextVobId = prefab.vobs.size() + 1;
    // The way points and freepoints next to its vobs (a guard post with its freepoints), relative.
    const auto taken = [&](const world::WaynetPoint& p)
    { return std::ranges::any_of(positions, [&](const Vec3& at) { return near(p.position, at, reach); }); };
    world::WaynetData entries;
    for (const world::WaynetPoint& p : waynet.points)
    {
        if (taken(p))
        {
            world::WaynetPoint copy = p;
            copy.position -= origin;
            copy.generated = false;
            entries.points.push_back(std::move(copy));
        }
    }
    for (const world::WaynetPoint& p : waynet.freepoints)
    {
        if (taken(p))
        {
            world::WaynetPoint copy = p;
            copy.position -= origin;
            copy.generated = false;
            entries.freepoints.push_back(std::move(copy));
        }
    }
    for (const world::WaynetEdge& e : waynet.edges)
    {
        if (entries.findPoint(e.a) != nullptr && entries.findPoint(e.b) != nullptr)
        {
            entries.edges.push_back({e.a, e.b, false});
        }
    }
    if (!entries.empty())
    {
        if (auto checked = world::normalizeWaynet(entries, "prefab"); !checked)
        {
            return checked.error();
        }
        prefab.waynet = std::move(entries);
    }
    return prefab;
}

std::string writePrefab(const world::WorldFile& prefab)
{
    return world::writeWorldFile(prefab);
}

Result<world::WorldFile> parsePrefab(std::string_view text, std::string_view source)
{
    return world::parseWorldFile(text, source);
}

std::vector<std::pair<std::string, std::string>> prefabWaynetNames(const world::WaynetData& waynet,
                                                                   const world::WorldFile& prefab)
{
    std::vector<std::pair<std::string, std::string>> names;
    if (!prefab.waynet)
    {
        return names;
    }
    world::WaynetData taken = waynet; // with the names handed out so far
    for (const auto* list : {&prefab.waynet->points, &prefab.waynet->freepoints})
    {
        for (const world::WaynetPoint& p : *list)
        {
            std::string stem = nameStem(p.name);
            std::string name = suggestName(taken, prefixOf(p.name), stem.empty() ? "NEU" : stem);
            taken.points.push_back({name, Vec3(0.0f), std::nullopt, false});
            names.emplace_back(p.name, std::move(name));
        }
    }
    return names;
}

Result<PlacedPrefab> placePrefab(world::Scene& scene, world::WaynetData& waynet,
                                 const world::WorldFile& prefab, const Vec3& at, f32 yawDegrees)
{
    PlacedPrefab placed;
    const Quat turn = glm::angleAxis(glm::radians(yawDegrees), Vec3(0.0f, 1.0f, 0.0f));
    // The waynet first (on a copy): nothing is placed if it does not fit.
    world::WaynetData edited = waynet;
    const auto names = prefabWaynetNames(waynet, prefab);
    const auto renamed = [&](std::string_view old)
    {
        const auto it = std::ranges::find(names, old, &std::pair<std::string, std::string>::first);
        return it != names.end() ? it->second : std::string(old);
    };
    if (prefab.waynet)
    {
        for (const world::WaynetPoint& p : prefab.waynet->points)
        {
            if (auto added = addPoint(edited, renamed(p.name), at + turn * p.position); !added)
            {
                return added.error();
            }
            placed.waynetNames.push_back(renamed(p.name));
        }
        for (const world::WaynetPoint& p : prefab.waynet->freepoints)
        {
            const Vec3 dir = turn * p.dir.value_or(Vec3(0.0f, 0.0f, -1.0f));
            if (auto added = addFreepoint(edited, renamed(p.name), at + turn * p.position, dir); !added)
            {
                return added.error();
            }
            placed.waynetNames.push_back(renamed(p.name));
        }
        for (const world::WaynetEdge& e : prefab.waynet->edges)
        {
            if (auto joined = connect(edited, renamed(e.a), renamed(e.b)); !joined)
            {
                return joined.error();
            }
            placed.waynetEdges.push_back(edgeKey(renamed(e.a), renamed(e.b)));
        }
    }
    // The vobs with new ids, parents inside the template mapped.
    std::map<u64, world::VobId> ids;
    for (const world::WorldFileVob& source : prefab.vobs)
    {
        world::WorldFileVob v = source;
        v.id = world::VobId{scene.nextVobId()};
        if (source.parent.valid())
        {
            const auto parent = ids.find(source.parent.value);
            if (parent == ids.end())
            {
                return Error{std::format("prefab vob {}: its parent {} comes later", source.id.value,
                                         source.parent.value)};
            }
            v.parent = parent->second;
        }
        else
        {
            v.transform.position = at + turn * source.transform.position;
            v.transform.rotation = glm::normalize(turn * source.transform.rotation);
        }
        if (auto restored = world::restoreVob(scene, v); !restored)
        {
            return restored.error();
        }
        if (scene.nextVobId() <= v.id.value)
        {
            (void)scene.setNextVobId(v.id.value + 1);
        }
        ids[source.id.value] = v.id;
        placed.vobs.push_back(v.id);
    }
    waynet = std::move(edited);
    return placed;
}
} // namespace g7::editor

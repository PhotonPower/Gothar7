#include <g7/editor/WaynetEdit.hpp>

#include <algorithm>
#include <format>

namespace g7::editor
{
namespace
{
world::WaynetPoint* findEntry(world::WaynetData& waynet, std::string_view name)
{
    for (auto* list : {&waynet.points, &waynet.freepoints})
    {
        for (world::WaynetPoint& p : *list)
        {
            if (p.name == name)
            {
                return &p;
            }
        }
    }
    return nullptr;
}

bool nameTaken(const world::WaynetData& waynet, std::string_view name)
{
    return waynet.findPoint(name) != nullptr || waynet.findFreepoint(name) != nullptr;
}

/// Checks and sorts `edited`; only then it replaces `target`.
Result<void> commit(world::WaynetData& target, world::WaynetData edited)
{
    if (auto checked = world::normalizeWaynet(edited, "editor"); !checked)
    {
        return checked;
    }
    target = std::move(edited);
    return {};
}
} // namespace

std::pair<std::string, std::string> edgeKey(std::string_view a, std::string_view b)
{
    return a < b ? std::pair{std::string(a), std::string(b)} : std::pair{std::string(b), std::string(a)};
}

WaynetSnapshot captureWaynet(const world::WaynetData& waynet, std::span<const std::string> names,
                             std::span<const std::pair<std::string, std::string>> edges)
{
    WaynetSnapshot s;
    s.names.assign(names.begin(), names.end());
    for (const auto& e : edges)
    {
        s.edges.push_back(edgeKey(e.first, e.second));
    }
    for (const std::string& name : names)
    {
        if (const world::WaynetPoint* p = waynet.findPoint(name))
        {
            s.entries.points.push_back(*p);
        }
        if (const world::WaynetPoint* p = waynet.findFreepoint(name))
        {
            s.entries.freepoints.push_back(*p);
        }
    }
    for (const world::WaynetEdge& e : waynet.edges)
    {
        const auto key = edgeKey(e.a, e.b);
        const bool atName =
            std::ranges::find(names, e.a) != names.end() || std::ranges::find(names, e.b) != names.end();
        if (atName && std::ranges::find(s.edges, key) == s.edges.end())
        {
            s.edges.push_back(key); // edges at a touched point are touched too
        }
        if (std::ranges::find(s.edges, key) != s.edges.end())
        {
            s.entries.edges.push_back(e);
        }
    }
    std::ranges::sort(s.edges);
    return s;
}

Result<void> restoreWaynet(world::WaynetData& target, const WaynetSnapshot& snapshot)
{
    world::WaynetData waynet = target; // all or nothing: a failed edit leaves the target as it was
    // Away with what the snapshot names, then its entries back.
    const auto named = [&](const world::WaynetPoint& p)
    { return std::ranges::find(snapshot.names, p.name) != snapshot.names.end(); };
    std::erase_if(waynet.points, named);
    std::erase_if(waynet.freepoints, named);
    std::erase_if(waynet.edges,
                  [&](const world::WaynetEdge& e)
                  {
                      // Edges at a named point are part of it, also ones made after the snapshot (a rename).
                      return std::ranges::find(snapshot.edges, edgeKey(e.a, e.b)) != snapshot.edges.end() ||
                             std::ranges::find(snapshot.names, e.a) != snapshot.names.end() ||
                             std::ranges::find(snapshot.names, e.b) != snapshot.names.end();
                  });
    waynet.points.insert(waynet.points.end(), snapshot.entries.points.begin(), snapshot.entries.points.end());
    waynet.freepoints.insert(waynet.freepoints.end(), snapshot.entries.freepoints.begin(),
                             snapshot.entries.freepoints.end());
    waynet.edges.insert(waynet.edges.end(), snapshot.entries.edges.begin(), snapshot.entries.edges.end());
    return commit(target, std::move(waynet));
}

std::string nameStem(std::string_view name)
{
    std::string_view rest = name;
    if (rest.starts_with("WP_"))
    {
        rest.remove_prefix(3);
    }
    else if (rest.starts_with("FP_"))
    {
        rest.remove_prefix(3);
        const usize type = rest.find('_');
        rest = type == std::string_view::npos ? std::string_view() : rest.substr(type + 1);
    }
    // Without a trailing number "_NN".
    if (const usize last = rest.rfind('_');
        last != std::string_view::npos && last + 1 < rest.size() &&
        std::ranges::all_of(rest.substr(last + 1), [](char c) { return c >= '0' && c <= '9'; }))
    {
        rest = rest.substr(0, last);
    }
    return std::string(rest);
}

std::string suggestName(const world::WaynetData& waynet, std::string_view prefix, std::string_view stem)
{
    const std::string base = std::string(prefix) + (stem.empty() ? std::string("NEU") : std::string(stem));
    for (u32 n = 1;; ++n)
    {
        std::string name = std::format("{}_{:02}", base, n);
        if (!nameTaken(waynet, name))
        {
            return name;
        }
    }
}

Result<void> addPoint(world::WaynetData& target, std::string name, const Vec3& position)
{
    world::WaynetData waynet = target; // all or nothing: a failed edit leaves the target as it was
    if (nameTaken(waynet, name))
    {
        return Error{std::format("the name {} is taken", name)};
    }
    waynet.points.push_back({std::move(name), position, std::nullopt, false});
    return commit(target, std::move(waynet));
}

Result<void> addFreepoint(world::WaynetData& target, std::string name, const Vec3& position, const Vec3& dir)
{
    world::WaynetData waynet = target; // all or nothing: a failed edit leaves the target as it was
    if (nameTaken(waynet, name))
    {
        return Error{std::format("the name {} is taken", name)};
    }
    waynet.freepoints.push_back({std::move(name), position, dir, false});
    return commit(target, std::move(waynet));
}

Result<void> moveEntry(world::WaynetData& target, std::string_view name, const Vec3& position,
                       std::optional<Vec3> dir)
{
    world::WaynetData waynet = target; // all or nothing: a failed edit leaves the target as it was
    world::WaynetPoint* p = findEntry(waynet, name);
    if (p == nullptr)
    {
        return Error{std::format("no point {}", name)};
    }
    p->position = position;
    if (dir)
    {
        p->dir = *dir;
    }
    p->generated = false; // contract: moved by the editor, it belongs to the human
    return commit(target, std::move(waynet));
}

Result<void> renameEntry(world::WaynetData& target, std::string_view from, std::string to)
{
    world::WaynetData waynet = target; // all or nothing: a failed edit leaves the target as it was
    world::WaynetPoint* p = findEntry(waynet, from);
    if (p == nullptr)
    {
        return Error{std::format("no point {}", from)};
    }
    if (to != from && nameTaken(waynet, to))
    {
        return Error{std::format("the name {} is taken", to)};
    }
    const std::string old(from);
    p->name = to;
    p->generated = false;
    for (world::WaynetEdge& e : waynet.edges)
    {
        e.a = e.a == old ? to : e.a;
        e.b = e.b == old ? to : e.b;
    }
    return commit(target, std::move(waynet));
}

Result<void> removeEntry(world::WaynetData& waynet, std::string_view name)
{
    const usize before = waynet.points.size() + waynet.freepoints.size();
    std::erase_if(waynet.points, [&](const world::WaynetPoint& p) { return p.name == name; });
    std::erase_if(waynet.freepoints, [&](const world::WaynetPoint& p) { return p.name == name; });
    if (waynet.points.size() + waynet.freepoints.size() == before)
    {
        return Error{std::format("no point {}", name)};
    }
    std::erase_if(waynet.edges, [&](const world::WaynetEdge& e) { return e.a == name || e.b == name; });
    return {};
}

Result<bool> connect(world::WaynetData& waynet, std::string_view a, std::string_view b)
{
    if (waynet.findPoint(a) == nullptr || waynet.findPoint(b) == nullptr || a == b)
    {
        return Error{std::format("edges join two different way points ({}, {})", a, b)};
    }
    const auto key = edgeKey(a, b);
    for (const world::WaynetEdge& e : waynet.edges)
    {
        if (edgeKey(e.a, e.b) == key)
        {
            return false;
        }
    }
    waynet.edges.push_back({key.first, key.second, false});
    if (auto normalized = world::normalizeWaynet(waynet, "editor"); !normalized)
    {
        return normalized.error();
    }
    return true;
}

Result<bool> disconnect(world::WaynetData& waynet, std::string_view a, std::string_view b)
{
    const auto key = edgeKey(a, b);
    const usize before = waynet.edges.size();
    std::erase_if(waynet.edges, [&](const world::WaynetEdge& e) { return edgeKey(e.a, e.b) == key; });
    return waynet.edges.size() != before;
}

bool isGeneratedEntry(const world::WaynetData& waynet, std::string_view name)
{
    const world::WaynetPoint* p = waynet.findPoint(name);
    p = p != nullptr ? p : waynet.findFreepoint(name);
    return p != nullptr && p->generated;
}

std::string removalAnnotation(std::string_view name)
{
    return std::format("\"{}\",", name);
}
} // namespace g7::editor

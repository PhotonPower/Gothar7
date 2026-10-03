#include <g7/world/Waynet.hpp>

#include <algorithm>
#include <format>
#include <map>

namespace g7::world
{
const WaynetPoint* WaynetData::findPoint(std::string_view name) const noexcept
{
    const auto it =
        std::find_if(points.begin(), points.end(), [&](const WaynetPoint& p) { return p.name == name; });
    return it == points.end() ? nullptr : &*it;
}

const WaynetPoint* WaynetData::findFreepoint(std::string_view name) const noexcept
{
    const auto it = std::find_if(freepoints.begin(), freepoints.end(),
                                 [&](const WaynetPoint& p) { return p.name == name; });
    return it == freepoints.end() ? nullptr : &*it;
}

std::string_view freepointType(std::string_view name) noexcept
{
    if (!name.starts_with("FP_"))
    {
        return {};
    }
    const std::string_view rest = name.substr(3);
    return rest.substr(0, rest.find('_'));
}

bool validWaynetName(std::string_view name, std::string_view prefix) noexcept
{
    return name.size() > prefix.size() && name.starts_with(prefix) &&
           std::all_of(name.begin(), name.end(),
                       [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}

Result<void> normalizeWaynet(WaynetData& waynet, std::string_view where)
{
    const auto fail = [&](std::string_view entry, std::string_view what)
    { return Error{std::format("{}: waynet.{}: {}", where, entry, what)}; };
    std::map<std::string, std::string, std::less<>> seen; // name -> entry, over points and freepoints
    for (const auto& [list, prefix, key] :
         {std::tuple{&waynet.points, std::string_view("WP_"), "points"},
          std::tuple{&waynet.freepoints, std::string_view("FP_"), "freepoints"}})
    {
        for (usize i = 0; i < list->size(); ++i)
        {
            const std::string entry = std::format("{}[{}]", key, i);
            WaynetPoint& p = (*list)[i];
            if (!validWaynetName(p.name, prefix))
            {
                return fail(entry, std::format("name \"{}\" must start with {} and use A-Z, 0-9 and _",
                                               p.name, prefix));
            }
            if (const auto [it, added] = seen.emplace(p.name, entry); !added)
            {
                return fail(entry, std::format("name \"{}\" is already used by {}", p.name, it->second));
            }
            if (p.dir)
            {
                Vec3 d(p.dir->x, 0.0f, p.dir->z);
                p.dir = glm::length(d) > 1e-6f ? std::optional<Vec3>(glm::normalize(d)) : std::nullopt;
            }
        }
    }
    for (usize i = 0; i < waynet.edges.size(); ++i)
    {
        WaynetEdge& e = waynet.edges[i];
        const std::string entry = std::format("edges[{}]", i);
        for (const std::string& end : {e.a, e.b})
        {
            if (waynet.findPoint(end) == nullptr)
            {
                return fail(entry, std::format("unknown point \"{}\"", end));
            }
        }
        if (e.a == e.b)
        {
            return fail(entry, std::format("connects \"{}\" with itself", e.a));
        }
        if (e.b < e.a)
        {
            std::swap(e.a, e.b);
        }
    }
    const auto byName = [](const WaynetPoint& x, const WaynetPoint& y) { return x.name < y.name; };
    std::sort(waynet.points.begin(), waynet.points.end(), byName);
    std::sort(waynet.freepoints.begin(), waynet.freepoints.end(), byName);
    // Duplicate edges: one stays, generated only if all copies were.
    std::sort(waynet.edges.begin(), waynet.edges.end(), [](const WaynetEdge& x, const WaynetEdge& y)
              { return std::tie(x.a, x.b) < std::tie(y.a, y.b); });
    std::vector<WaynetEdge> merged;
    for (const WaynetEdge& e : waynet.edges)
    {
        if (!merged.empty() && merged.back().a == e.a && merged.back().b == e.b)
        {
            merged.back().generated = merged.back().generated && e.generated;
            continue;
        }
        merged.push_back(e);
    }
    waynet.edges = std::move(merged);
    return {};
}
} // namespace g7::world

#include <g7/ai/Waynet.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <queue>

namespace g7::ai
{
namespace
{
bool sameName(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                              [](char x, char y)
                                              {
                                                  return std::tolower(static_cast<unsigned char>(x)) ==
                                                         std::tolower(static_cast<unsigned char>(y));
                                              });
}
} // namespace

f32 Route::length(const Vec3& from) const noexcept
{
    f32 total = 0.0f;
    Vec3 at = from;
    for (const Vec3& p : points)
    {
        total += glm::length(p - at);
        at = p;
    }
    return total;
}

Waynet Waynet::build(const world::WaynetData& data)
{
    Waynet net;
    for (const world::WaynetPoint& p : data.points)
    {
        net.m_points.push_back({p.name, p.position, p.dir});
    }
    for (const world::WaynetPoint& p : data.freepoints)
    {
        net.m_freepoints.push_back({p.name, std::string(world::freepointType(p.name)), p.position, p.dir});
    }
    net.m_edges.resize(net.m_points.size());
    for (const world::WaynetEdge& e : data.edges)
    {
        const auto a = net.find(e.a);
        const auto b = net.find(e.b);
        if (a && b && *a != *b)
        {
            net.m_edges[*a].push_back(*b);
            net.m_edges[*b].push_back(*a);
        }
    }
    return net;
}

std::optional<u32> Waynet::find(std::string_view name) const
{
    for (u32 i = 0; i < m_points.size(); ++i)
    {
        if (sameName(m_points[i].name, name))
        {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<u32> Waynet::findFreepoint(std::string_view name) const
{
    for (u32 i = 0; i < m_freepoints.size(); ++i)
    {
        if (sameName(m_freepoints[i].name, name))
        {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<u32>> Waynet::path(u32 from, u32 to) const
{
    if (from >= m_points.size() || to >= m_points.size())
    {
        return std::nullopt;
    }
    const auto n = m_points.size();
    std::vector<f32> cost(n, std::numeric_limits<f32>::infinity());
    std::vector<u32> previous(n, ~0u);
    using Entry = std::pair<f32, u32>; // estimated total, point
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    const auto estimate = [&](u32 p) { return glm::length(m_points[p].position - m_points[to].position); };
    cost[from] = 0.0f;
    open.push({estimate(from), from});
    while (!open.empty())
    {
        const auto [guess, p] = open.top();
        open.pop();
        if (p == to)
        {
            break;
        }
        if (guess > cost[p] + estimate(p) + 1e-4f)
        {
            continue; // an outdated entry
        }
        for (const u32 q : m_edges[p])
        {
            const f32 through = cost[p] + glm::length(m_points[q].position - m_points[p].position);
            if (through < cost[q])
            {
                cost[q] = through;
                previous[q] = p;
                open.push({through + estimate(q), q});
            }
        }
    }
    if (cost[to] == std::numeric_limits<f32>::infinity())
    {
        return std::nullopt;
    }
    std::vector<u32> result;
    for (u32 p = to; p != ~0u; p = previous[p])
    {
        result.push_back(p);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::optional<u32> Waynet::nearest(const Vec3& position, const WalkableLine& walkable, f32 maxDistance) const
{
    // Nearest first; the first one with a walkable line wins.
    std::vector<std::pair<f32, u32>> byDistance;
    for (u32 i = 0; i < m_points.size(); ++i)
    {
        const f32 d = glm::length(m_points[i].position - position);
        if (d <= maxDistance)
        {
            byDistance.push_back({d, i});
        }
    }
    std::sort(byDistance.begin(), byDistance.end());
    for (const auto& [d, i] : byDistance)
    {
        if (!walkable || walkable(position, m_points[i].position))
        {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<Route> Waynet::route(const Vec3& from, const Vec3& goal, const WalkableLine& walkable) const
{
    if (!walkable || walkable(from, goal))
    {
        return Route{{goal}, {}};
    }
    const auto start = nearest(from, walkable);
    // The goal end: the nearest point from which the goal can be walked to.
    const auto end = nearest(goal, [&](const Vec3& g, const Vec3& p) { return walkable(p, g); });
    if (!start || !end)
    {
        return std::nullopt;
    }
    const auto points = path(*start, *end);
    if (!points)
    {
        return std::nullopt;
    }
    // Smoothing: from where we stand, skip ahead to the furthest point still walkable in a line.
    Route route;
    std::vector<Vec3> along;
    for (const u32 p : *points)
    {
        along.push_back(m_points[p].position);
    }
    along.push_back(goal);
    std::vector<u32> ids = *points;
    ids.push_back(~0u);
    Vec3 at = from;
    usize i = 0;
    while (i < along.size())
    {
        usize furthest = i;
        for (usize j = along.size() - 1; j > i; --j)
        {
            if (walkable(at, along[j]))
            {
                furthest = j;
                break;
            }
        }
        route.points.push_back(along[furthest]);
        if (ids[furthest] != ~0u)
        {
            route.waypoints.push_back(ids[furthest]);
        }
        at = along[furthest];
        i = furthest + 1;
    }
    return route;
}
} // namespace g7::ai

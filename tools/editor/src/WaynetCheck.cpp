#include <g7/editor/WaynetCheck.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <numeric>

namespace g7::editor
{
std::vector<WaynetIssue> checkWaynet(const world::WaynetData& waynet, const WaynetCheckSettings& s,
                                     const BlockedFn& blocked, const GroundFn& ground)
{
    using Kind = WaynetIssue::Kind;
    std::vector<WaynetIssue> issues;
    std::map<std::string, usize, std::less<>> index;
    for (usize i = 0; i < waynet.points.size(); ++i)
    {
        index.emplace(waynet.points[i].name, i);
    }
    // Connected parts (union-find over the edges).
    std::vector<usize> parent(waynet.points.size());
    std::iota(parent.begin(), parent.end(), usize{0});
    const auto root = [&](usize i)
    {
        while (parent[i] != i)
        {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };
    std::vector<u32> degree(waynet.points.size(), 0);
    const Vec3 hip(0.0f, s.hipHeight, 0.0f);
    for (const world::WaynetEdge& e : waynet.edges)
    {
        const auto a = index.find(e.a);
        const auto b = index.find(e.b);
        if (a == index.end() || b == index.end())
        {
            continue; // normalizeWaynet refuses such edges anyway
        }
        ++degree[a->second];
        ++degree[b->second];
        parent[root(a->second)] = root(b->second);
        const Vec3 pa = waynet.points[a->second].position;
        const Vec3 pb = waynet.points[b->second].position;
        const f32 length = glm::length(pb - pa);
        const Vec3 middle = (pa + pb) * 0.5f;
        if (length < s.minEdge)
        {
            issues.push_back(
                {Kind::ShortEdge, e.a, e.b, middle,
                 std::format("{} - {}: {:.1f} m (under {:.0f} m)", e.a, e.b, length, s.minEdge)});
        }
        else if (length > s.maxEdge)
        {
            issues.push_back({Kind::LongEdge, e.a, e.b, middle,
                              std::format("{} - {}: {:.1f} m (over {:.0f} m)", e.a, e.b, length, s.maxEdge)});
        }
        if (blocked && blocked(pa + hip, pb + hip))
        {
            issues.push_back({Kind::Blocked, e.a, e.b, middle,
                              std::format("{} - {}: something solid at hip height", e.a, e.b)});
        }
    }
    std::map<usize, usize> partSize;
    for (usize i = 0; i < waynet.points.size(); ++i)
    {
        ++partSize[root(i)];
    }
    usize largest = waynet.points.size();
    usize largestSize = 0;
    for (const auto& [part, size] : partSize)
    {
        if (size > largestSize)
        {
            largest = part;
            largestSize = size;
        }
    }
    for (usize i = 0; i < waynet.points.size(); ++i)
    {
        const world::WaynetPoint& p = waynet.points[i];
        if (degree[i] == 0)
        {
            issues.push_back({Kind::Unconnected, p.name, {}, p.position, std::format("{}: no edge", p.name)});
        }
        else if (root(i) != largest)
        {
            issues.push_back({Kind::Island,
                              p.name,
                              {},
                              p.position,
                              std::format("{}: not connected to the main net ({} of {} points)", p.name,
                                          partSize[root(i)], waynet.points.size())});
        }
        if (ground && !ground(p.position))
        {
            issues.push_back(
                {Kind::NotOnGround, p.name, {}, p.position, std::format("{}: no walkable ground", p.name)});
        }
    }
    for (const world::WaynetPoint& fp : waynet.freepoints)
    {
        const bool near =
            std::ranges::any_of(waynet.points, [&](const world::WaynetPoint& p)
                                { return glm::length(p.position - fp.position) <= s.freepointReach; });
        if (!near)
        {
            issues.push_back({Kind::LoneFreepoint,
                              fp.name,
                              {},
                              fp.position,
                              std::format("{}: no way point within {:.0f} m", fp.name, s.freepointReach)});
        }
        if (ground && !ground(fp.position))
        {
            issues.push_back({Kind::NotOnGround,
                              fp.name,
                              {},
                              fp.position,
                              std::format("{}: no walkable ground", fp.name)});
        }
    }
    std::ranges::stable_sort(issues, [](const WaynetIssue& a, const WaynetIssue& b)
                             { return a.kind != b.kind ? a.kind < b.kind : a.name < b.name; });
    return issues;
}
} // namespace g7::editor

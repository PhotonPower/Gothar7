#pragma once

// The waynet as a graph for path finding (M9 part A, docs/modules/ai.md "Wegnetz & Navigation"): way points
// and freepoints of the world's waynet block (world.md "Wegnetz"), A* over the undirected edges, the nearest
// point one can walk to, routes from any position to any point with shortcuts where nothing is in the way.

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>
#include <g7/world/Waynet.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::ai
{
/// Whether one can walk straight from `a` to `b` (no wall, no drop): the engine asks the physics; tests
/// answer what they like. Empty: always.
using WalkableLine = std::function<bool(const Vec3& a, const Vec3& b)>;

/// A way to walk: the points in order (the last one is the goal), and the waypoints it uses.
struct Route
{
    std::vector<Vec3> points;
    std::vector<u32> waypoints; ///< indices into Waynet::points(), in order (empty: walked straight)
    [[nodiscard]] f32 length(const Vec3& from) const noexcept;
};

class Waynet
{
public:
    struct Point
    {
        std::string name;
        Vec3 position{0.0f};
        std::optional<Vec3> dir;
    };
    struct Freepoint
    {
        std::string name;
        std::string type; ///< "SIT" for FP_SIT_..., upper case
        Vec3 position{0.0f};
        std::optional<Vec3> dir;
    };

    Waynet() = default;
    /// From a world's (checked) waynet block.
    [[nodiscard]] static Waynet build(const world::WaynetData& data);

    [[nodiscard]] const std::vector<Point>& points() const noexcept { return m_points; }
    [[nodiscard]] const std::vector<Freepoint>& freepoints() const noexcept { return m_freepoints; }
    [[nodiscard]] bool empty() const noexcept { return m_points.empty(); }
    /// Index of a way point by name, without regard to case ("wp_camp_gate" finds "WP_CAMP_GATE").
    [[nodiscard]] std::optional<u32> find(std::string_view name) const;
    /// Index of a freepoint by name, without regard to case.
    [[nodiscard]] std::optional<u32> findFreepoint(std::string_view name) const;
    [[nodiscard]] const std::vector<u32>& neighbours(u32 point) const { return m_edges[point]; }

    /// A* from point to point; the points of the path including both ends, nullopt if unconnected.
    [[nodiscard]] std::optional<std::vector<u32>> path(u32 from, u32 to) const;
    /// The nearest point one can walk to straight from `position` (up to `maxDistance`), nullopt if none.
    [[nodiscard]] std::optional<u32> nearest(const Vec3& position, const WalkableLine& walkable = {},
                                             f32 maxDistance = 1e9f) const;
    /// A route from `from` to `goal`: straight if walkable, else to the nearest reachable point, along the
    /// net to the point nearest the goal, then to the goal; points are skipped where the line past them is
    /// walkable. nullopt if neither straight nor over the net.
    [[nodiscard]] std::optional<Route> route(const Vec3& from, const Vec3& goal,
                                             const WalkableLine& walkable = {}) const;

private:
    std::vector<Point> m_points;
    std::vector<Freepoint> m_freepoints;
    std::vector<std::vector<u32>> m_edges;
};
} // namespace g7::ai

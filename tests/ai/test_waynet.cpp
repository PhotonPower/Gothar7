// Waynet (M9 part A): graph from the waynet block, names without regard to case, A*, nearest walkable point,
// routes with shortcuts.

#include <g7/ai/Waynet.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;
using namespace g7::ai;

namespace
{
/// A U around a wall: A (0,0) - B (0,10) - C (10,10) - D (10,0); a wall between A and D (x = 5, z < 8).
world::WaynetData uNet()
{
    world::WaynetData data;
    data.points = {{"WP_A", Vec3(0, 0, 0)},
                   {"WP_B", Vec3(0, 0, 10)},
                   {"WP_C", Vec3(10, 0, 10)},
                   {"WP_D", Vec3(10, 0, 0)},
                   {"WP_LONELY", Vec3(50, 0, 50)}};
    data.edges = {{"WP_A", "WP_B"}, {"WP_B", "WP_C"}, {"WP_C", "WP_D"}};
    data.freepoints = {{"FP_SIT_BENCH_01", Vec3(1, 0, 1)}};
    REQUIRE(world::normalizeWaynet(data, "test").ok());
    return data;
}

/// The wall at x = 5 for z < 8 blocks lines that cross it there.
bool walkable(const Vec3& a, const Vec3& b)
{
    if ((a.x - 5.0f) * (b.x - 5.0f) >= 0.0f)
    {
        return true; // same side
    }
    const f32 t = (5.0f - a.x) / (b.x - a.x);
    return a.z + t * (b.z - a.z) >= 8.0f;
}
} // namespace

TEST_CASE("Waynet: names without regard to case, freepoint types")
{
    const Waynet net = Waynet::build(uNet());
    REQUIRE(net.points().size() == 5);
    CHECK(net.find("wp_c").has_value());
    CHECK(net.points()[*net.find("wp_c")].name == "WP_C");
    CHECK_FALSE(net.find("wp_x").has_value());
    REQUIRE(net.findFreepoint("fp_sit_bench_01").has_value());
    CHECK(net.freepoints()[0].type == "SIT");
}

TEST_CASE("Waynet: A* along the edges, unconnected points have no path")
{
    const Waynet net = Waynet::build(uNet());
    const u32 a = *net.find("WP_A");
    const u32 d = *net.find("WP_D");
    const auto path = net.path(a, d);
    REQUIRE(path.has_value());
    std::vector<std::string> names;
    for (const u32 p : *path)
    {
        names.push_back(net.points()[p].name);
    }
    CHECK(names == std::vector<std::string>{"WP_A", "WP_B", "WP_C", "WP_D"});
    CHECK(net.path(a, a)->size() == 1);
    CHECK_FALSE(net.path(a, *net.find("WP_LONELY")).has_value());
}

TEST_CASE("Waynet: nearest walkable point, routes around the wall with shortcuts")
{
    const Waynet net = Waynet::build(uNet());
    // Next to D but on A's side of the wall: D is nearer, A is the one to walk to.
    CHECK(*net.nearest(Vec3(4, 0, 1), walkable) == *net.find("WP_A"));
    CHECK(*net.nearest(Vec3(4, 0, 1)) == *net.find("WP_A")); // without a test, simply the nearest
    CHECK(*net.nearest(Vec3(6, 0, 1)) == *net.find("WP_D"));

    // Straight where nothing is in the way.
    const auto straight = net.route(Vec3(1, 0, 1), Vec3(2, 0, 5), walkable);
    REQUIRE(straight.has_value());
    CHECK(straight->points.size() == 1);
    CHECK(straight->waypoints.empty());

    // Around the wall: from near A to near D. The way through B and C, shortened where the line is free.
    const auto around = net.route(Vec3(1, 0, 1), Vec3(9, 0, 1), walkable);
    REQUIRE(around.has_value());
    CHECK(around->points.back() == Vec3(9, 0, 1));
    for (usize i = 0; i + 1 < around->points.size(); ++i)
    {
        CHECK(walkable(around->points[i], around->points[i + 1]));
    }
    CHECK(walkable(Vec3(1, 0, 1), around->points.front()));
    // A is skipped (B can be walked to straight from the start), the route goes over B and C.
    CHECK(std::find(around->waypoints.begin(), around->waypoints.end(), *net.find("WP_A")) ==
          around->waypoints.end());
    CHECK(around->length(Vec3(1, 0, 1)) < 30.0f);

    // Nothing connects to the lonely point.
    const auto blocked = [](const Vec3& a, const Vec3& b) { return glm::length(a - b) < 12.0f; };
    CHECK_FALSE(net.route(Vec3(0, 0, 0), Vec3(50, 0, 49), blocked).has_value());
}

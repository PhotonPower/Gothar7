// Visibility (M4): distance and size culling, the cull grid.

#include <g7/render/Camera.hpp>
#include <g7/render/Visibility.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

using namespace g7;
using namespace g7::render;

namespace
{
AABB boxAt(const Vec3& centre, f32 half)
{
    return AABB{centre - Vec3(half), centre + Vec3(half)};
}
} // namespace

TEST_CASE("Visibility: distance to the nearest point of the bounds, size relative to distance")
{
    const AABB unit = boxAt(Vec3(0.0f), 1.0f);
    CHECK(distanceTo(unit, Vec3(0.5f)) == 0.0f); // inside
    CHECK(distanceTo(unit, Vec3(4.0f, 0.0f, 0.0f)) == doctest::Approx(3.0f));

    const CullSettings settings{.viewDistance = 100.0f, .sizeCull = 0.01f};
    const Vec3 eye(0.0f);
    // A 1 m cube (radius 0.87) is kept up to 87 m when size-cullable, to 100 m otherwise.
    CHECK(cullByDistance(boxAt(Vec3(0, 0, -50), 0.5f), eye, settings, true) == CullResult::Kept);
    CHECK(cullByDistance(boxAt(Vec3(0, 0, -95), 0.5f), eye, settings, true) == CullResult::TooSmall);
    CHECK(cullByDistance(boxAt(Vec3(0, 0, -95), 0.5f), eye, settings, false) == CullResult::Kept);
    CHECK(cullByDistance(boxAt(Vec3(0, 0, -120), 0.5f), eye, settings, false) == CullResult::TooFar);
    // A house (20 m) far away is still large enough; the distance is to its nearest point.
    CHECK(cullByDistance(boxAt(Vec3(0, 0, -105), 10.0f), eye, settings, true) == CullResult::Kept);
    // 0 turns each test off.
    CHECK(cullByDistance(boxAt(Vec3(0, 0, -5000), 0.1f), eye, {}, true) == CullResult::Kept);
    // Standing inside an object never hides it.
    CHECK(cullByDistance(boxAt(Vec3(0.0f), 0.01f), Vec3(0.0f), settings, true) == CullResult::Kept);
}

TEST_CASE("Visibility: the grid returns the objects of cells in the frustum, large objects included")
{
    // 20 x 20 small boxes 10 m apart over 200 m, and one long wall reaching from x 0 to x 190.
    std::vector<AABB> bounds;
    for (int z = 0; z < 20; ++z)
    {
        for (int x = 0; x < 20; ++x)
        {
            bounds.push_back(boxAt(Vec3(f32(x) * 10.0f, 0.0f, f32(z) * -10.0f), 0.5f));
        }
    }
    bounds.push_back(AABB{Vec3(0.0f, 0.0f, -1.0f), Vec3(190.0f, 5.0f, 0.0f)}); // centre in a cell at x 95
    const u32 wall = static_cast<u32>(bounds.size() - 1);
    CullGrid grid;
    grid.build(bounds, 64.0f);
    CHECK(grid.cellCount() == 12); // x cells 0..2 (x 0..190) times z cells 0..-3 (z 0..-190)

    // A camera at x 5 looking along -X sees little of the field but the start of the wall, whose centre
    // (x 95) lies in another cell: that cell's bounds grow to cover the whole wall.
    Camera camera;
    camera.aspect = 1.0f;
    camera.transform.position = Vec3(5.0f, 2.0f, 0.5f);
    camera.transform.rotation = lookRotation(Vec3(-1.0f, 0.0f, 0.0f));
    std::vector<u32> out;
    grid.query(camera.frustum(), camera.transform.position, 0.0f, out);
    CHECK(std::find(out.begin(), out.end(), wall) != out.end()); // the wall's cell reaches x 0
    CHECK(out.size() < bounds.size() / 2);

    // Looking along -Z over the field: everything in front; a distance limit drops far cells.
    camera.transform.rotation = lookRotation(Vec3(0.3f, 0.0f, -1.0f));
    out.clear();
    grid.query(camera.frustum(), camera.transform.position, 0.0f, out);
    const usize all = out.size();
    out.clear();
    grid.query(camera.frustum(), camera.transform.position, 70.0f, out);
    CHECK(out.size() < all);
    CHECK(grid.visitedCells() < grid.cellCount());

    grid.build({}, 64.0f);
    CHECK(grid.cellCount() == 0);
}

TEST_CASE("Visibility: level of detail by distance with hysteresis, forced levels")
{
    const LodSettings s; // 60 m, 150 m, 10 %
    CHECK(selectLod(10.0f, 0, s) == 0);
    CHECK(selectLod(65.0f, 0, s) == 0); // past 60 m but within the hysteresis (66 m)
    CHECK(selectLod(67.0f, 0, s) == 1);
    CHECK(selectLod(55.0f, 1, s) == 1); // back below 60 m: stays until 54 m
    CHECK(selectLod(53.0f, 1, s) == 0);
    CHECK(selectLod(200.0f, 0, s) == 2); // far at once: straight to the coarsest
    CHECK(selectLod(140.0f, 2, s) == 2);
    CHECK(selectLod(130.0f, 2, s) == 1);
    CHECK(selectLod(10.0f, 2, s) == 0);
    LodSettings forced = s;
    forced.forced = 1;
    CHECK(selectLod(1.0f, 0, forced) == 1);
    CHECK(selectLod(500.0f, 2, forced) == 1);
}

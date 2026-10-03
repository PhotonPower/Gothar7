// Physics (M5, Jolt): static shapes, height field with holes, queries with layer filters.

#include <g7/physics/Physics.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace g7;
using namespace g7::physics;

namespace
{
/// The 8 corners of a box as hull points.
ShapePart box(const Vec3& min, const Vec3& max)
{
    ShapePart part;
    part.kind = ShapePart::Kind::Hull;
    for (int i = 0; i < 8; ++i)
    {
        part.points.push_back(Vec3(i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z));
    }
    return part;
}

PhysicsWorld makeWorld()
{
    auto world = PhysicsWorld::create({.maxBodies = 1024, .threads = 1});
    REQUIRE_MESSAGE(world.ok(), (world.ok() ? "" : world.error().message));
    return std::move(world).value();
}
} // namespace

TEST_CASE("Physics: rays and spheres hit shared, placed and scaled static shapes")
{
    PhysicsWorld world = makeWorld();
    // A 2 x 2 x 2 box shape, used twice: at x 0 and, scaled 3 in y and turned, at x 10.
    const std::vector<ShapePart> parts{box(Vec3(-1.0f), Vec3(1.0f))};
    const ShapeId shape = world.createShape(parts).value();
    REQUIRE(
        world.addStatic(shape, Vec3(0.0f, 1.0f, 0.0f), Quat(1, 0, 0, 0), Vec3(1.0f), Layer::World, 7).ok());
    REQUIRE(world
                .addStatic(shape, Vec3(10.0f, 3.0f, 0.0f), glm::angleAxis(0.5f, Vec3(0, 1, 0)),
                           Vec3(1.0f, 3.0f, 1.0f), Layer::World, 8)
                .ok());
    world.optimize();
    CHECK(world.stats().bodies == 2);
    CHECK(world.stats().shapes == 1);

    // Straight down onto the first box: its top is at y 2.
    const auto hit = world.raycast(Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 50.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->distance == doctest::Approx(8.0f).epsilon(0.001));
    CHECK(hit->normal.y == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(hit->userData == 7);
    // The scaled one reaches up to y 6.
    const auto tall = world.raycast(Vec3(10.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 50.0f);
    REQUIRE(tall.has_value());
    CHECK(tall->position.y == doctest::Approx(6.0f).epsilon(0.001));
    CHECK(tall->userData == 8);
    CHECK_FALSE(world.raycast(Vec3(5.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 50.0f).has_value());
    CHECK_FALSE(
        world.raycast(Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 5.0f).has_value()); // too short

    // A sphere of radius 0.5 sliding along +x at y 1 touches the box side (x 1) half a metre earlier.
    const auto sphere = world.sphereCast(Vec3(-5.0f, 1.0f, 0.0f), 0.5f, Vec3(1.0f, 0.0f, 0.0f), 20.0f);
    REQUIRE(sphere.has_value());
    CHECK(sphere->distance == doctest::Approx(3.5f).epsilon(0.01)); // centre stops at x -1.5
    CHECK(sphere->normal.x == doctest::Approx(-1.0f).epsilon(0.01));
    const auto touching = world.overlapSphere(Vec3(5.0f, 1.0f, 0.0f), 6.0f);
    CHECK(touching.size() == 2);
    CHECK(world.overlapSphere(Vec3(5.0f, 1.0f, 0.0f), 1.0f).empty());
}

TEST_CASE("Physics: meshes, compounds, layers and removal")
{
    PhysicsWorld world = makeWorld();
    // A floor of two triangles plus, in the same shape, a hull pillar.
    ShapePart floor;
    floor.points = {Vec3(-5, 0, -5), Vec3(5, 0, -5), Vec3(5, 0, 5), Vec3(-5, 0, 5)};
    floor.indices = {0, 2, 1, 0, 3, 2, 0, 0, 1}; // the last one is degenerate and skipped
    const std::vector<ShapePart> parts{floor, box(Vec3(2, 0, 2), Vec3(3, 4, 3))};
    const ShapeId compound = world.createShape(parts).value();
    CHECK(world.stats().triangles == 2);
    const BodyId body =
        world.addStatic(compound, Vec3(0.0f), Quat(1, 0, 0, 0), Vec3(1.0f), Layer::World, 1).value();
    // Water above the floor: the default query sees it, a filtered one not.
    const ShapeId waterBox =
        world.createShape(std::vector<ShapePart>{box(Vec3(-1, 0, -1), Vec3(1, 1, 1))}).value();
    REQUIRE(world.addStatic(waterBox, Vec3(0, 1, 0), Quat(1, 0, 0, 0), Vec3(1.0f), Layer::Water, 2).ok());
    CHECK(world.raycast(Vec3(0, 10, 0), Vec3(0, -1, 0), 20.0f)->userData == 2);
    const auto ground =
        world.raycast(Vec3(0, 10, 0), Vec3(0, -1, 0), 20.0f, kAllLayers & ~layerBit(Layer::Water));
    REQUIRE(ground.has_value());
    CHECK(ground->userData == 1);
    CHECK(ground->distance == doctest::Approx(10.0f).epsilon(0.001));
    CHECK(world.raycast(Vec3(2.5f, 10, 2.5f), Vec3(0, -1, 0), 20.0f)->position.y ==
          doctest::Approx(4.0f).epsilon(0.001));

    world.remove(body);
    CHECK(world.stats().bodies == 1);
    CHECK_FALSE(world.raycast(Vec3(3, 10, -3), Vec3(0, -1, 0), 20.0f).has_value());
    world.clear();
    CHECK(world.stats().bodies == 0);
    CHECK(world.stats().shapes == 0);

    CHECK_FALSE(world.createShape({}).ok());
    ShapePart broken;
    broken.indices = {0, 1};
    CHECK_FALSE(world.createShape(std::vector<ShapePart>{broken}).ok());
    CHECK_FALSE(world.addStatic(ShapeId{42}, Vec3(0.0f), Quat(1, 0, 0, 0), Vec3(1.0f), Layer::World, 0).ok());
}

TEST_CASE("Physics: height field - heights, odd sizes, holes never larger than drawn")
{
    PhysicsWorld world = makeWorld();
    // 5 x 4 samples (odd, not square: padded), 2 m apart from (-4, -3); height = x (a slope).
    const u32 w = 5;
    const u32 h = 4;
    std::vector<f32> heights;
    for (u32 r = 0; r < h; ++r)
    {
        for (u32 c = 0; c < w; ++c)
        {
            heights.push_back(-4.0f + 2.0f * static_cast<f32>(c)); // y = world x
        }
    }
    // Holes: 4 x 3 cells; the 2 x 2 block at cells (1..2, 0..1) is a hole.
    std::vector<u8> holes(static_cast<usize>(w - 1) * (h - 1), 255);
    for (const auto& [c, r] : {std::pair<usize, usize>{1, 0}, {2, 0}, {1, 1}, {2, 1}})
    {
        holes[r * (w - 1) + c] = 0;
    }
    REQUIRE(world.addHeightfield({w, h, 2.0f, Vec2(-4.0f, -3.0f), heights, {}}, 99).ok());
    const auto slope = world.raycast(Vec3(1.0f, 20.0f, 0.5f), Vec3(0, -1, 0), 50.0f);
    REQUIRE(slope.has_value());
    CHECK(std::abs(slope->position.y - 1.0f) < 0.02f); // Jolt quantises heights (8 bits per 2 x 2 block)
    CHECK(slope->userData == 99);
    CHECK_FALSE(world.raycast(Vec3(10.0f, 20.0f, 0.0f), Vec3(0, -1, 0), 50.0f).has_value()); // the padding

    world.clear();
    REQUIRE(world.addHeightfield({w, h, 2.0f, Vec2(-4.0f, -3.0f), heights, holes}).ok());
    // Only sample (2, 1) at x 0, z -1 has hole cells all around: the cells next to it fall through ...
    CHECK_FALSE(world.raycast(Vec3(-0.5f, 20.0f, -1.5f), Vec3(0, -1, 0), 50.0f).has_value());
    CHECK_FALSE(world.raycast(Vec3(0.5f, 20.0f, -0.5f), Vec3(0, -1, 0), 50.0f).has_value());
    // ... while ground outside the drawn hole stays solid.
    CHECK(world.raycast(Vec3(-3.0f, 20.0f, 2.0f), Vec3(0, -1, 0), 50.0f).has_value());
    CHECK(world.raycast(Vec3(3.0f, 20.0f, -2.0f), Vec3(0, -1, 0), 50.0f).has_value());

    CHECK_FALSE(world.addHeightfield({1, 4, 1.0f, Vec2(0.0f), heights, {}}).ok());
    CHECK_FALSE(world.addHeightfield({w, h, 2.0f, Vec2(0.0f), heights, std::vector<u8>(3)}).ok());
}

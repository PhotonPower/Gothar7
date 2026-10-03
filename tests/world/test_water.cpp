// Water (M5 part E): the vob type in .g7world and the surface query.

#include <g7/world/Scene.hpp>
#include <g7/world/Water.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <ostream>
#include <string>

using namespace g7;
using namespace g7::world;

namespace
{
std::string errorOf(std::string_view vob)
{
    auto world =
        parseWorldFile(std::string(R"({"version": 1, "vobs": [)") + std::string(vob) + "]}", "w.g7world");
    REQUIRE_FALSE(world.ok());
    return world.error().message;
}

// A pond (surface 0, 2 m deep, 10 x 6 m) and, turned by 90 degrees and overlapping it, a river section
// whose surface lies higher (0.5).
const char* const kWater = R"({"version": 1, "name": "water", "vobs": [
  {"id": 1, "type": "water", "name": "WATER_POND", "pos": [0, -1, 0], "components": {"water": {"halfExtents": [5, 1, 3]}}},
  {"id": 2, "type": "water", "name": "WATER_RIVER", "pos": [8, -0.5, 0], "rot": [0, 0.707107, 0, 0.707107],
   "components": {"water": {"halfExtents": [10, 1, 2], "kind": "river"}}}
]})";
} // namespace

TEST_CASE("Water vobs: read, spawned, captured and written unchanged")
{
    auto file = parseWorldFile(kWater, "water.g7world");
    REQUIRE_MESSAGE(file.ok(), (file.ok() ? "" : file.error().message));
    REQUIRE(file.value().vobs.size() == 2);
    const WorldFileVob& pond = file.value().vobs[0];
    CHECK(pond.type == VobType::Water);
    CHECK(pond.water.halfExtents == Vec3(5.0f, 1.0f, 3.0f));
    CHECK(pond.water.kind.empty());
    CHECK(file.value().vobs[1].water.kind == "river");

    Scene scene;
    REQUIRE(spawnWorld(scene, file.value()).ok());
    const WorldFile captured = captureWorld(scene, "water");
    REQUIRE(captured.vobs.size() == 2);
    const auto river = std::find_if(captured.vobs.begin(), captured.vobs.end(),
                                    [](const WorldFileVob& v) { return v.name == "WATER_RIVER"; });
    REQUIRE(river != captured.vobs.end());
    CHECK(river->type == VobType::Water);
    CHECK(river->water.kind == "river");
    const std::string written = writeWorldFile(captured);
    auto again = parseWorldFile(written, "again.g7world");
    REQUIRE(again.ok());
    CHECK(writeWorldFile(again.value()) == written);
    CHECK(written.find(R"("components":{"water":{"halfExtents":[5.0,1.0,3.0]}})") != std::string::npos);
}

TEST_CASE("Water vobs: errors")
{
    CHECK(errorOf(R"({"id": 1, "type": "water"})") ==
          "w.g7world: vobs[0].components.water: needs 'halfExtents'");
    CHECK(errorOf(R"({"id": 1, "type": "water", "components": {"water": {"halfExtents": [1, 0, 1]}}})") ==
          "w.g7world: vobs[0].components.water: 'halfExtents' must be positive");
    CHECK(errorOf(R"({"id": 1, "type": "water", "rot": [0.258819, 0, 0, 0.965926],
                     "components": {"water": {"halfExtents": [1, 1, 1]}}})") ==
          "w.g7world: vobs[0]: water may only be turned about Y (its top is the surface)");
    CHECK(errorOf(R"({"id": 1, "type": "water", "scale": [2, 1, 1],
                     "components": {"water": {"halfExtents": [1, 1, 1]}}})") ==
          "w.g7world: vobs[0]: water is not scaled - its size is 'halfExtents'");
}

TEST_CASE("Water bodies: surface over a point, turned boxes, overlaps, dry above")
{
    auto file = parseWorldFile(kWater, "water.g7world");
    REQUIRE(file.ok());
    Scene scene;
    REQUIRE(spawnWorld(scene, file.value()).ok());
    scene.updateTransforms();
    WaterBodies water;
    water.rebuild(scene);
    REQUIRE(water.bodies().size() == 2);
    for (const WaterBody& body : water.bodies())
    {
        CHECK(body.yaw == doctest::Approx(body.vob.value == 2 ? glm::radians(90.0f) : 0.0f).epsilon(0.001));
    }

    CHECK(water.surfaceAt(Vec3(0.0f, -1.5f, 0.0f)) == doctest::Approx(0.0f)); // in the pond
    CHECK(water.surfaceAt(Vec3(4.9f, -1.5f, 2.9f)).has_value());              // its corner
    CHECK_FALSE(water.surfaceAt(Vec3(4.9f, -1.5f, 3.1f)).has_value());        // just outside (z)
    CHECK_FALSE(water.surfaceAt(Vec3(0.0f, -2.5f, 0.0f)).has_value());        // below the bottom
    CHECK(water.surfaceAt(Vec3(0.0f, 0.4f, 0.0f)).has_value());               // just above the surface
    CHECK_FALSE(water.surfaceAt(Vec3(0.0f, 3.0f, 0.0f)).has_value());         // on a bridge: dry
    // The river is turned by 90 degrees: 2 m wide along x, 10 m long along z.
    CHECK(water.surfaceAt(Vec3(8.0f, -1.0f, 9.0f)) == doctest::Approx(0.5f));
    CHECK_FALSE(water.surfaceAt(Vec3(11.0f, -1.0f, 0.0f)).has_value());
    // Where both overlap (x 6..7), the higher surface wins.
    CHECK(water.surfaceAt(Vec3(6.5f, -1.0f, 0.0f)) == doctest::Approx(0.5f));
    water.clear();
    CHECK_FALSE(water.surfaceAt(Vec3(0.0f, -1.5f, 0.0f)).has_value());
}

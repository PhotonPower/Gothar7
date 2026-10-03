// Editor operations on the world (M4): placing, duplicating, deleting, world transforms under a
// parent, picking by ray; the generator head of .g7world.

#include <g7/editor/Operations.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::editor;

namespace
{
world::WorldFile parse(std::string_view text)
{
    auto world = world::parseWorldFile(text, "w.g7world");
    REQUIRE_MESSAGE(world.ok(), (world.ok() ? "" : world.error().message));
    return std::move(world).value();
}

const char* const kWorld = R"({"version": 1, "nextVobId": 20, "vobs": [
  {"id": 1, "type": "empty", "name": "HUT", "pos": [10, 0, 0], "rot": [0, 0.707107, 0, 0.707107], "scale": [2, 2, 2]},
  {"id": 2, "type": "mesh", "name": "WALL", "parent": 1, "pos": [1, 0, 0], "mesh": "models/wall.glb", "category": "gameplay"},
  {"id": 3, "type": "start", "name": "START_A", "pos": [0, 0, -10]},
  {"id": 4, "type": "trigger", "name": "TRG_A", "pos": [0, 1, -20], "components": {"trigger": {"halfExtents": [2, 1, 2]}}},
  {"id": 5, "type": "light", "name": "LAMP", "pos": [5, 2, -10], "components": {"light": {"range": 4}}}
]})";
} // namespace

TEST_CASE("Editor operations: world transforms keep the parent")
{
    world::Scene scene;
    REQUIRE(world::spawnWorld(scene, parse(kWorld)).ok());
    const entt::entity wall = scene.findById(world::VobId{2});
    // HUT is turned 90 degrees about Y and scaled 2: WALL's local (1, 0, 0) lies at (10, 0, -2) in the world.
    const Transform before = worldTransformOf(scene, wall);
    CHECK(before.position.x == doctest::Approx(10.0f).epsilon(0.001));
    CHECK(before.position.z == doctest::Approx(-2.0f).epsilon(0.001));
    CHECK(before.scale.x == doctest::Approx(2.0f).epsilon(0.001));

    Transform target = before;
    target.position = Vec3(12.0f, 1.0f, -4.0f);
    setWorldTransform(scene, wall, target);
    CHECK(scene.parent(wall) == scene.findById(world::VobId{1})); // still under the hut
    const Transform after = worldTransformOf(scene, wall);
    CHECK(after.position.x == doctest::Approx(12.0f).epsilon(0.001));
    CHECK(after.position.y == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(after.position.z == doctest::Approx(-4.0f).epsilon(0.001));
    CHECK(std::as_const(scene).get<Transform>(wall)->position.x ==
          doctest::Approx(2.0f).epsilon(0.001)); // local: (12-10)/2 along the turned axis
}

TEST_CASE("Editor operations: place, duplicate and delete use and keep ids")
{
    world::Scene scene;
    REQUIRE(world::spawnWorld(scene, parse(kWorld)).ok());
    auto placed = placeMesh(scene, "testscene/nature/rock_largeA.glb", Vec3(3.0f, 0.0f, 4.0f));
    REQUIRE(placed.ok());
    CHECK(scene.idOf(placed.value()) == world::VobId{20}); // from nextVobId
    CHECK(scene.get<world::Vob>(placed.value())->nameText == "ROCK_LARGEA");
    CHECK(scene.get<world::MeshRef>(placed.value())->category == world::VobCategory::Deco);

    const entt::entity wall = scene.findById(world::VobId{2});
    auto copy = duplicateVob(scene, wall, Vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(copy.ok());
    CHECK(scene.idOf(copy.value()) == world::VobId{21});
    CHECK(scene.parent(copy.value()) == scene.parent(wall));
    CHECK(scene.get<world::MeshRef>(copy.value())->path == "models/wall.glb");
    CHECK(scene.get<world::MeshRef>(copy.value())->category == world::VobCategory::Gameplay);
    CHECK(std::as_const(scene).get<Transform>(copy.value())->position.x == doctest::Approx(2.0f));
    auto trigger = duplicateVob(scene, scene.findById(world::VobId{4}), Vec3(0.0f));
    REQUIRE(trigger.ok());
    CHECK(scene.get<world::TriggerVolume>(trigger.value())->halfExtents == Vec3(2.0f, 1.0f, 2.0f));

    // Deleting the hut removes its children; their ids are never handed out again.
    removeVob(scene, scene.findById(world::VobId{1}));
    CHECK_FALSE(scene.valid(scene.findById(world::VobId{2})));
    CHECK_FALSE(scene.valid(scene.findById(world::VobId{21})));
    auto next = placeMesh(scene, "a.glb", Vec3(0.0f));
    REQUIRE(next.ok());
    CHECK(scene.idOf(next.value()) == world::VobId{23});
    CHECK(world::captureWorld(scene, "w").nextVobId == 24);
}

TEST_CASE("Editor operations: picking hits meshes by bounds and markers of invisible vobs")
{
    world::Scene scene;
    REQUIRE(world::spawnWorld(scene, parse(kWorld)).ok());
    scene.updateTransforms();
    // One drawn mesh (bounds as the engine would have them) in front of a start point.
    std::vector<SceneInstance> instances(1);
    instances[0].vob = world::VobId{2};
    instances[0].bounds = AABB{Vec3(-1.0f, 0.0f, -6.0f), Vec3(1.0f, 2.0f, -5.0f)};
    const Ray towardsMinusZ{Vec3(0.0f, 0.85f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)};
    CHECK(pick(scene, instances, towardsMinusZ) ==
          world::VobId{2});                                   // the mesh hides the start point behind it
    CHECK(pick(scene, {}, towardsMinusZ) == world::VobId{3}); // START_A at z -10, marker at eye height / 2
    const Ray atTrigger{Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)};
    CHECK(pick(scene, {}, Ray{Vec3(0.0f, 1.5f, -14.0f), Vec3(0.0f, 0.0f, -1.0f)}) ==
          world::VobId{4}); // the trigger box
    CHECK(pick(scene, {}, Ray{Vec3(5.0f, 2.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)}) ==
          world::VobId{5}); // the lamp
    CHECK_FALSE(pick(scene, {}, Ray{Vec3(50.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)}).valid());
    CHECK(intersect(atTrigger, AABB{Vec3(-1.0f), Vec3(1.0f)}).value() == 0.0f); // starting inside
}

TEST_CASE("Generator head: kept unchanged, owned ids answer isGenerated, broken ones are ignored")
{
    const world::WorldFile file = parse(R"({"version": 1, "nextVobId": 40,
      "generator": {"tool": "gothar-worldgen", "owned": [3, [10, 12], 30]}, "vobs": []})");
    CHECK(world::isGenerated(file, world::VobId{3}));
    CHECK(world::isGenerated(file, world::VobId{11}));
    CHECK(world::isGenerated(file, world::VobId{30}));
    CHECK_FALSE(world::isGenerated(file, world::VobId{13}));
    const std::string written = world::writeWorldFile(file);
    CHECK(
        written.find(
            "\"nextVobId\": 40,\n  \"generator\": {\"tool\":\"gothar-worldgen\",\"owned\":[3,[10,12],30]}") !=
        std::string::npos);
    CHECK(world::writeWorldFile(parse(written)) == written);

    // Stale or broken: still loads (a hint only), nothing counts as generated.
    const world::WorldFile broken =
        parse(R"({"version": 1, "generator": {"owned": [[5, 2], "x"]}, "vobs": []})");
    CHECK_FALSE(world::isGenerated(broken, world::VobId{3}));
    CHECK(world::writeWorldFile(broken).find("\"generator\": {\"owned\":[[5,2],\"x\"]}") !=
          std::string::npos);
    CHECK_FALSE(world::isGenerated(parse(R"({"version": 1})"), world::VobId{1}));
}

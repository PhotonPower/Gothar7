// Undo and redo of the editor (M16 part A, ADR 0022): commands keep the states of the vobs they touch (their
// lines of the world file); undo and redo put them back byte for byte - for placing, moving, editing,
// deleting with children; typing joins one step; the depth is limited; the saved version is known.

#include <g7/editor/Commands.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::editor;

namespace
{
const char* const kWorld = R"({"version": 1, "nextVobId": 20, "vobs": [
  {"id": 1, "type": "empty", "name": "HUT", "pos": [10, 0, 0], "rot": [0, 0.707107, 0, 0.707107], "scale": [2, 2, 2]},
  {"id": 2, "type": "mesh", "name": "WALL", "parent": 1, "pos": [1, 0, 0], "mesh": "models/wall.glb", "category": "gameplay"},
  {"id": 3, "type": "mesh", "name": "ROOF", "parent": 2, "pos": [0, 2, 0], "mesh": "models/roof.glb"},
  {"id": 4, "type": "trigger", "name": "TRG_A", "pos": [0, 1, -20], "components": {"trigger": {"halfExtents": [2, 1, 2]}}},
  {"id": 5, "type": "light", "name": "LAMP", "pos": [5, 2, -10], "components": {"light": {"range": 4}}}
]})";

void load(world::Scene& scene)
{
    auto world = world::parseWorldFile(kWorld, "w.g7world");
    REQUIRE(world.ok());
    REQUIRE(world::spawnWorld(scene, world.value()).ok());
    scene.updateTransforms();
}

/// The scene as the editor would save it.
std::string text(const world::Scene& scene)
{
    return world::writeWorldFile(world::captureWorld(scene, "w"));
}

/// Does `change` on `ids` as the editor does: states before, the change, states after (with created vobs).
Command run(world::Scene& scene, std::string label, std::vector<world::VobId> ids, const auto& change,
            bool mergeable = false)
{
    const u64 firstNew = scene.nextVobId();
    Command c{std::move(label), captureVobs(scene, ids), {}, mergeable};
    change();
    for (u64 id = firstNew; id < scene.nextVobId(); ++id)
    {
        ids.push_back(world::VobId{id});
        c.before.push_back({world::VobId{id}, std::nullopt});
    }
    c.after = captureVobs(scene, ids);
    scene.updateTransforms();
    return c;
}
} // namespace

TEST_CASE("Undo: a vob is captured with its children; restoring brings back the same lines")
{
    world::Scene scene;
    load(scene);
    const std::vector<world::VobId> wall{world::VobId{2}};
    const std::vector<VobState> states = captureVobs(scene, wall);
    REQUIRE(states.size() == 2); // WALL and its ROOF
    CHECK(states[0].id == world::VobId{2});
    CHECK(states[1].id == world::VobId{3});
    CHECK(states[0].json->find("\"WALL\"") != std::string::npos);
    const std::vector<world::VobId> missing{world::VobId{99}};
    CHECK_FALSE(captureVobs(scene, missing)[0].json.has_value());

    // Destroyed and put back: the same file.
    const std::string original = text(scene);
    removeVob(scene, scene.findById(world::VobId{2}));
    CHECK_FALSE(scene.valid(scene.findById(world::VobId{3})));
    REQUIRE(restoreVobs(scene, states).ok());
    CHECK(text(scene) == original);
}

TEST_CASE("Undo: placing, moving, editing and deleting go back and forth byte for byte")
{
    world::Scene scene;
    load(scene);
    History history;
    const std::string start = text(scene);

    // Place a mesh (a new id), move the hut (its children follow), edit the light, delete the wall with its
    // roof.
    history.record(
        run(scene, "place", {}, [&] { REQUIRE(placeMesh(scene, "models/rock.glb", Vec3(3, 0, 3)).ok()); }));
    const std::string placed = text(scene);
    CHECK(placed.find("ROCK") != std::string::npos);
    history.record(run(scene, "move HUT", {world::VobId{1}},
                       [&]
                       {
                           Transform t = worldTransformOf(scene, scene.findById(world::VobId{1}));
                           t.position.x += 5.0f;
                           setWorldTransform(scene, scene.findById(world::VobId{1}), t);
                       }));
    const std::string moved = text(scene);
    history.record(run(scene, "edit LAMP", {world::VobId{5}}, [&]
                       { scene.get<world::LightSource>(scene.findById(world::VobId{5}))->range = 9.0f; }));
    const std::string edited = text(scene);
    history.record(run(scene, "delete WALL", {world::VobId{2}},
                       [&] { removeVob(scene, scene.findById(world::VobId{2})); }));
    const std::string deleted = text(scene);
    CHECK(deleted.find("ROOF") == std::string::npos);
    CHECK(history.undoLabel() == "delete WALL");

    REQUIRE(history.undo(scene).value());
    CHECK(text(scene) == edited); // wall and roof back, under the moved hut
    REQUIRE(history.undo(scene).value());
    CHECK(text(scene) == moved);
    REQUIRE(history.undo(scene).value());
    CHECK(text(scene) == placed);
    REQUIRE(history.undo(scene).value());
    CHECK(text(scene).find("ROCK") == std::string::npos);
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.undo(scene).value());
    // The ids stay used: undoing a placement does not lower the counter.
    CHECK(scene.nextVobId() == 21);

    for (const std::string* expected : {&placed, &moved, &edited, &deleted})
    {
        REQUIRE(history.redo(scene).value());
        CHECK(text(scene) == *expected);
    }
    CHECK_FALSE(history.canRedo());
    (void)start;
}

TEST_CASE("Undo: a new edit drops the redo steps; typing joins one step; the depth is limited")
{
    world::Scene scene;
    load(scene);
    History history(3);
    const auto lamp = [&](f32 range)
    {
        return run(
            scene, "edit range of LAMP", {world::VobId{5}},
            [&] { scene.get<world::LightSource>(scene.findById(world::VobId{5}))->range = range; }, true);
    };
    const std::string start = text(scene);
    history.record(lamp(5.0f));
    history.record(lamp(6.0f)); // typing on: the same step
    history.record(lamp(7.0f));
    CHECK(history.size() == 1);
    REQUIRE(history.undo(scene).value());
    CHECK(text(scene) == start); // back to before the first keystroke
    REQUIRE(history.redo(scene).value());
    CHECK(scene.get<world::LightSource>(scene.findById(world::VobId{5}))->range == 7.0f);

    REQUIRE(history.undo(scene).value());
    history.record(run(scene, "edit TRG_A", {world::VobId{4}}, [&]
                       { scene.get<world::TriggerVolume>(scene.findById(world::VobId{4}))->once = true; }));
    CHECK_FALSE(history.canRedo()); // the undone lamp edit is gone

    for (int i = 0; i < 5; ++i)
    {
        history.record(run(scene, "move TRG_A", {world::VobId{4}},
                           [&]
                           {
                               Transform t = worldTransformOf(scene, scene.findById(world::VobId{4}));
                               t.position.z += 1.0f;
                               setWorldTransform(scene, scene.findById(world::VobId{4}), t);
                           }));
    }
    CHECK(history.size() == 3); // the oldest dropped
}

TEST_CASE("Undo: the saved version is known after undo and redo")
{
    world::Scene scene;
    load(scene);
    History history;
    CHECK(history.atSaved()); // as loaded
    const auto move = [&]
    {
        return run(scene, "move LAMP", {world::VobId{5}},
                   [&]
                   {
                       Transform t = worldTransformOf(scene, scene.findById(world::VobId{5}));
                       t.position.y += 1.0f;
                       setWorldTransform(scene, scene.findById(world::VobId{5}), t);
                   });
    };
    history.record(move());
    CHECK_FALSE(history.atSaved());
    history.markSaved();
    CHECK(history.atSaved());
    REQUIRE(history.undo(scene).value());
    CHECK_FALSE(history.atSaved()); // differs from the saved file
    REQUIRE(history.redo(scene).value());
    CHECK(history.atSaved());
    REQUIRE(history.undo(scene).value());
    history.record(move()); // another branch: the saved version cannot come back by redo
    CHECK_FALSE(history.atSaved());
}

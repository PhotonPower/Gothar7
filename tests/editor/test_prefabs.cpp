// Prefabs and vobs from Blender (M16 part F): a template of vobs with children and the waynet entries near
// them, round trip through the file, loose copies with new ids and renamed way points/freepoints, one undo
// step; the VOB_ markers of a .glb as child vobs (ADR 0023).

#include <g7/editor/Commands.hpp>
#include <g7/editor/GlbVobImport.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/editor/Prefabs.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::editor;

namespace
{
const char* const kWorld = R"({"version": 1, "nextVobId": 30, "vobs": [
  {"id": 10, "type": "mesh", "name": "CAMPFIRE", "pos": [20, 0, 10], "mesh": "models/fire.glb"},
  {"id": 11, "type": "mob", "name": "BENCH", "parent": 10, "pos": [0, 0, 2], "rot": [0, 0.707107, 0, 0.707107], "mesh": "mobs/bench.glb", "components": {"mob": {"definition": "bench"}}},
  {"id": 12, "type": "light", "name": "FIRE_LIGHT", "parent": 10, "pos": [0, 1, 0], "components": {"light": {"range": 6}}},
  {"id": 13, "type": "mesh", "name": "FAR_ROCK", "pos": [80, 0, 80], "mesh": "models/rock.glb"}
]})";

world::WaynetData waynet()
{
    world::WaynetData w;
    w.points = {{"WP_CAMP_FIRE", Vec3(21, 0, 10)}, {"WP_CAMP_FAR", Vec3(60, 0, 60)}};
    w.freepoints = {{"FP_SIT_CAMPFIRE_01", Vec3(20, 0, 11.5f), Vec3(0, 0, -1)}};
    w.edges = {{"WP_CAMP_FAR", "WP_CAMP_FIRE"}};
    REQUIRE(world::normalizeWaynet(w, "test").ok());
    return w;
}

void load(world::Scene& scene)
{
    auto world = world::parseWorldFile(kWorld, "w.g7world");
    REQUIRE(world.ok());
    REQUIRE(world::spawnWorld(scene, world.value()).ok());
    scene.updateTransforms();
}
} // namespace

TEST_CASE("Prefabs: a template of a vob with its children and the waynet next to it; the file round trip")
{
    world::Scene scene;
    load(scene);
    const world::WaynetData w = waynet();
    auto prefab = makePrefab(scene, std::vector{world::VobId{10}}, w, "campfire");
    REQUIRE(prefab.ok());
    const world::WorldFile& p = prefab.value();
    REQUIRE(p.vobs.size() == 3);
    CHECK(p.vobs[0].id.value == 1);
    CHECK_FALSE(p.vobs[0].parent.valid());
    CHECK(p.vobs[0].transform.position == Vec3(0.0f)); // relative to the first vob
    CHECK(p.vobs[1].parent.value == 1);                // children keep their parent inside the template
    CHECK(p.vobs[1].transform.position == Vec3(0, 0, 2));
    CHECK(p.nextVobId == 4);
    REQUIRE(p.waynet.has_value());
    CHECK(p.waynet->points.size() == 1); // WP_CAMP_FIRE (1 m away), not the far one
    CHECK(p.waynet->points[0].position == Vec3(1, 0, 0));
    CHECK(p.waynet->freepoints.size() == 1);
    CHECK(p.waynet->edges.empty()); // the edge leads out of the template
    CHECK_FALSE(makePrefab(scene, {}, w, "x").ok());

    const std::string text = writePrefab(p);
    auto back = parsePrefab(text, "campfire.g7prefab");
    REQUIRE(back.ok());
    CHECK(writePrefab(back.value()) == text);
}

TEST_CASE("Prefabs: loose copies - new ids, renamed waynet entries, turned; one undo step")
{
    world::Scene scene;
    load(scene);
    world::WaynetData w = waynet();
    const world::WorldFile prefab = makePrefab(scene, std::vector{world::VobId{10}}, w, "campfire").value();

    History history;
    const std::string before = world::writeWorldFile(world::captureWorld(scene, "w"));
    const world::WaynetData waynetBefore = w;
    const auto names = prefabWaynetNames(w, prefab);
    REQUIRE(names.size() == 2);
    CHECK(names[0].second == "WP_CAMP_FIRE_01");
    CHECK(names[1].second == "FP_SIT_CAMPFIRE_02"); // the next free number of its stem
    std::vector<std::string> newNames{names[0].second, names[1].second};
    Command command{"place prefab campfire", {}, {}, false};
    command.waynetBefore = captureWaynet(w, newNames);
    auto placed = placePrefab(scene, w, prefab, Vec3(100, 0, 0), 90.0f);
    REQUIRE(placed.ok());
    scene.updateTransforms();
    REQUIRE(placed.value().vobs.size() == 3);
    CHECK(placed.value().vobs[0].value == 30); // from nextVobId
    CHECK(scene.nextVobId() == 33);
    const entt::entity bench = scene.findById(placed.value().vobs[1]);
    CHECK(scene.idOf(scene.parent(bench)) == placed.value().vobs[0]);
    // Turned by 90 degrees: the way point 1 m east of the fire is 1 m north (-Z) of the copy.
    const world::WaynetPoint* wp = w.findPoint("WP_CAMP_FIRE_01");
    REQUIRE(wp != nullptr);
    CHECK(wp->position.x == doctest::Approx(100.0f));
    CHECK(wp->position.z == doctest::Approx(-1.0f));
    CHECK(w.findFreepoint("FP_SIT_CAMPFIRE_02") != nullptr);
    CHECK(w.findPoint("WP_CAMP_FIRE") != nullptr); // the original stays

    for (const world::VobId id : placed.value().vobs)
    {
        command.before.push_back({id, std::nullopt});
    }
    command.after = captureVobs(scene, placed.value().vobs);
    command.waynetAfter = captureWaynet(w, newNames, placed.value().waynetEdges);
    history.record(std::move(command));
    REQUIRE(history.undo(scene, &w).value());
    world::WorldFile undone = world::captureWorld(scene, "w");
    world::WorldFile original = world::parseWorldFile(before, "w").value();
    undone.nextVobId = original.nextVobId = 0; // ids handed out stay used
    CHECK(world::writeWorldFile(undone) == world::writeWorldFile(original));
    CHECK(w == waynetBefore);

    // A second copy numbers on.
    auto second = placePrefab(scene, w, prefab, Vec3(200, 0, 0));
    REQUIRE(second.ok());
    CHECK(second.value().vobs[0].value == 33);
}

TEST_CASE("Blender vobs: VOB_<type>_<name> markers become lights, start points and box triggers")
{
    const Mat4 trigger = glm::translate(Mat4(1.0f), Vec3(1, 1, 2)) *
                         glm::rotate(Mat4(1.0f), glm::radians(90.0f), Vec3(0, 1, 0)) *
                         glm::scale(Mat4(1.0f), Vec3(2.0f, 1.5f, 0.5f));
    const std::vector<VobMarker> markers{
        {"VOB_LIGHT_Hearth", glm::translate(Mat4(1.0f), Vec3(0, 1, 0)) * glm::scale(Mat4(1.0f), Vec3(2.0f))},
        {"VOB_START_Innen.001", glm::translate(Mat4(1.0f), Vec3(3, 0, 0))},
        {"VOB_TRIGGER_door", trigger},
        {"VOB_DRAGON_x", Mat4(1.0f)},
        {"VOB_LIGHT", Mat4(1.0f)},
    };
    const VobImport imported = vobsFromMarkers(markers);
    REQUIRE(imported.vobs.size() == 3);
    CHECK(imported.vobs[0].type == world::VobType::Light);
    CHECK(imported.vobs[0].name == "HEARTH");
    CHECK(imported.vobs[0].light.range == doctest::Approx(10.0f)); // 2 units of size x 5 m
    CHECK(imported.vobs[0].transform.scale == Vec3(1.0f));
    CHECK(imported.vobs[1].type == world::VobType::Start);
    CHECK(imported.vobs[1].name == "INNEN"); // Blender's ".001" dropped
    CHECK(imported.vobs[1].transform.position == Vec3(3, 0, 0));
    CHECK(imported.vobs[2].type == world::VobType::Trigger);
    CHECK(imported.vobs[2].trigger.halfExtents.x == doctest::Approx(2.0f));
    CHECK(imported.vobs[2].trigger.halfExtents.z == doctest::Approx(0.5f));
    const Vec3 x = imported.vobs[2].transform.rotation * Vec3(1, 0, 0);
    CHECK(x.z == doctest::Approx(-1.0f)); // turned 90 degrees about +Y
    CHECK(imported.warnings.size() == 2); // unknown type, no name
}

#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;
using namespace g7::world;

namespace
{
// Children before their parent on purpose: the loader must not depend on file order.
constexpr std::string_view kCamp = R"({
  "version": 1,
  "name": "camp",
  "nextVobId": 200,
  "staticMeshes": ["meshes/world/terrain.g7mesh"],
  "vobs": [
    { "id": 12, "type": "light", "name": "FIRE_LIGHT", "parent": 11, "pos": [0, 1, 0],
      "components": { "light": { "color": [1, 0.5, 0.2], "range": 10, "flicker": 0.3 } } },
    { "id": 11, "type": "mesh", "name": "CAMPFIRE", "parent": 10, "pos": [2, 0, 0], "mesh": "meshes/campfire.g7mesh" },
    { "id": 10, "type": "empty", "name": "CAMP", "pos": [100, 0, 50], "rot": [0, 0.7071068, 0, 0.7071068], "scale": [2, 2, 2] }
  ],
  "waynet": { "points": [ { "name": "WP_CAMP", "pos": [0, 0, 0] } ], "edges": [] },
  "zones": [ { "type": "music", "value": "CAMP" } ]
})";

WorldFile parse(std::string_view text)
{
    auto world = parseWorldFile(text, "camp.g7world");
    REQUIRE_MESSAGE(world.ok(), (world.ok() ? "" : world.error().message));
    return std::move(world).value();
}

std::string errorOf(std::string_view text)
{
    auto world = parseWorldFile(text, "w.g7world");
    REQUIRE_FALSE(world.ok());
    return world.error().message;
}
} // namespace

TEST_CASE("WorldFile: reads vobs, components and the id counter")
{
    const WorldFile world = parse(kCamp);
    CHECK(world.name == "camp");
    CHECK(world.nextVobId == 200);
    CHECK(world.staticMeshes == std::vector<std::string>{"meshes/world/terrain.g7mesh"});
    REQUIRE(world.vobs.size() == 3);
    const WorldFileVob& light = world.vobs[0];
    CHECK(light.type == VobType::Light);
    CHECK(light.parent == VobId{11});
    CHECK(light.light.range == 10.0f);
    CHECK(light.light.intensity == LightSource{}.intensity); // default
    CHECK(light.light.flicker == doctest::Approx(0.3f));
    CHECK(world.vobs[1].mesh == "meshes/campfire.g7mesh");
    CHECK(world.vobs[2].transform.scale == Vec3(2.0f));
    CHECK(world.vobs[2].transform.rotation.y == doctest::Approx(0.7071068f));
    CHECK_FALSE(world.waynetJson.empty());
    CHECK_FALSE(world.zonesJson.empty());
}

TEST_CASE("WorldFile: writing is stable and round-trips")
{
    const WorldFile world = parse(kCamp);
    const std::string text = writeWorldFile(world);
    // Sorted by id, so the hand-written order does not matter; reading it back gives the same bytes.
    CHECK(text.find("\"id\":10") < text.find("\"id\":11"));
    CHECK(text.find("\"id\":11") < text.find("\"id\":12"));
    CHECK(writeWorldFile(parse(text)) == text);
    CHECK(text.ends_with("\n"));
    // Waynet and zones come back unchanged.
    CHECK(parse(text).waynetJson == world.waynetJson);
    CHECK(parse(text).zonesJson == world.zonesJson);
}

TEST_CASE("WorldFile: the id counter lies above every id")
{
    const WorldFile world = parse(R"({"version": 1, "nextVobId": 2, "vobs": [ {"id": 7} ]})");
    CHECK(world.nextVobId == 8);
    CHECK(world.vobs[0].type == VobType::Empty); // the default type
}

TEST_CASE("WorldFile: errors name the file and the entry")
{
    CHECK(errorOf("not json") == "w.g7world: file: not valid JSON");
    CHECK(errorOf("[1, 2]") == "w.g7world: file: must be a JSON object");
    CHECK(errorOf(R"({"name": "x"})") == "w.g7world: version: missing");
    CHECK(errorOf(R"({"version": 2})") == "w.g7world: version: 2 is not supported (expected 1)");
    CHECK(errorOf(R"({"version": 1, "vobs": [ {"type": "mesh"} ]})") ==
          "w.g7world: vobs[0]: needs an 'id' (integer >= 1)");
    CHECK(errorOf(R"({"version": 1, "vobs": [ {"id": 0} ]})") ==
          "w.g7world: vobs[0]: needs an 'id' (integer >= 1)");
    CHECK(errorOf(R"({"version": 1, "vobs": [ {"id": 1}, {"id": 1} ]})") ==
          "w.g7world: vobs[1]: duplicate id 1");
    CHECK(errorOf(R"({"version": 1, "vobs": [ {"id": 1, "type": "dragon"} ]})") ==
          "w.g7world: vobs[0]: unknown type 'dragon'");
    CHECK(errorOf(R"({"version": 1, "vobs": [ {"id": 1, "type": "mesh"} ]})") ==
          "w.g7world: vobs[0]: a mesh vob needs 'mesh' (VFS path)");
    CHECK(errorOf(R"({"version": 1, "vobs": [ {"id": 1, "pos": [1, 2]} ]})") ==
          "w.g7world: vobs[0].pos: must be a list of 3 numbers");
    CHECK(errorOf(R"({"version": 1, "vobs": [ {"id": 1, "rot": [0, 0, 1]} ]})") ==
          "w.g7world: vobs[0].rot: must be a list of 4 numbers");
    CHECK(
        errorOf(
            R"({"version": 1, "vobs": [ {"id": 1, "type": "light", "components": {"light": {"range": 0}}} ]})") ==
        "w.g7world: vobs[0].components.light: 'range' must be positive");
}

TEST_CASE("WorldFile: spawning into a scene keeps ids and hierarchy")
{
    Scene scene;
    REQUIRE(spawnWorld(scene, parse(kCamp)).ok());
    CHECK(scene.vobCount() == 3);
    CHECK(scene.nextVobId() == 200);
    const auto camp = scene.findById(VobId{10});
    const auto fire = scene.findById(VobId{11});
    const auto light = scene.findById(VobId{12});
    REQUIRE(scene.valid(light));
    CHECK(scene.parent(light) == fire);
    CHECK(scene.parent(fire) == camp);
    CHECK(scene.get<MeshRef>(fire)->path == "meshes/campfire.g7mesh");
    CHECK(scene.get<LightSource>(light)->range == 10.0f);
    CHECK(scene.get<Vob>(fire)->nameText == "CAMPFIRE");
    scene.updateTransforms();
    // Camp at (100, 0, 50), turned 90° left and scaled 2: the fire's local +X (2 m) ends 4 m along -Z.
    const Vec3 firePosition(scene.get<WorldTransform>(fire)->matrix[3]);
    CHECK(firePosition.x == doctest::Approx(100.0f).epsilon(1e-4));
    CHECK(firePosition.z == doctest::Approx(46.0f).epsilon(1e-4));
    // New vobs continue after the file's counter.
    CHECK(scene.idOf(scene.spawnVob({"NEW"}).value()) == VobId{200});
}

TEST_CASE("WorldFile: broken hierarchies leave the scene untouched")
{
    Scene scene;
    CHECK_FALSE(spawnWorld(scene, parse(R"({"version": 1, "vobs": [ {"id": 1, "parent": 9} ]})")).ok());
    CHECK_FALSE(
        spawnWorld(scene,
                   parse(R"({"version": 1, "vobs": [ {"id": 1, "parent": 2}, {"id": 2, "parent": 1} ]})"))
            .ok());
    CHECK(scene.vobCount() == 0);
    REQUIRE(scene.spawnVob({"OLD", {}, {}, VobId{5}}).ok());
    CHECK_FALSE(spawnWorld(scene, parse(R"({"version": 1, "vobs": [ {"id": 5} ]})")).ok()); // id taken
    CHECK(scene.vobCount() == 1);
}

TEST_CASE("WorldFile: capturing a scene writes what spawning reads")
{
    Scene scene;
    REQUIRE(spawnWorld(scene, parse(kCamp)).ok());
    REQUIRE(scene.spawnVob({"ITEM", {}, {}, {}, true}).ok()); // runtime vobs are not part of the world
    const WorldFile captured = captureWorld(scene, "camp");
    CHECK(captured.vobs.size() == 3);
    CHECK(captured.nextVobId == 200);
    Scene again;
    REQUIRE(spawnWorld(again, captured).ok());
    CHECK(writeWorldFile(captureWorld(again, "camp")) == writeWorldFile(captured));
    CHECK(again.get<Vob>(again.findById(VobId{12}))->nameText == "FIRE_LIGHT");
}

TEST_CASE("WorldFile: one vob per line for readable diffs")
{
    const std::string text = writeWorldFile(parse(kCamp));
    usize vobLines = 0;
    usize start = 0;
    while (start < text.size())
    {
        const usize end = text.find('\n', start);
        const std::string_view line(text.data() + start, end - start);
        vobLines += line.starts_with("    {\"id\":") ? 1 : 0;
        start = end + 1;
    }
    CHECK(vobLines == 3);
    CHECK(writeWorldFile(WorldFile{}).find("\"vobs\": []") != std::string::npos);
}

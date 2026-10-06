#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

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
    REQUIRE(world.waynet.has_value());
    CHECK(world.waynet->points.size() == 1);
    REQUIRE(world.zones.size() == 1);
    CHECK(world.zones[0].type == "music");
    CHECK_FALSE(world.zones[0].box.has_value());
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
    REQUIRE(parse(text).waynet.has_value());
    CHECK(parse(text).waynet->points[0].name == "WP_CAMP");
    REQUIRE(parse(text).zones.size() == 1);
    CHECK(parse(text).zones[0].json == world.zones[0].json);
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

TEST_CASE("WorldFile: waynet by name - points, freepoints, edges, owner, stable lines")
{
    const char* text = R"({ "version": 1, "vobs": [],
  "waynet": {
    "points": [ { "name": "WP_MARKT_02", "pos": [20, 3, -41.5], "owner": "worldgen" },
                { "name": "WP_MARKT_01", "pos": [12.5, 3.1, -40], "dir": [0, 0.5, 2], "owner": "worldgen" },
                { "name": "WP_KIRCHE_TUER", "pos": [30.2, 4, -38] } ],
    "edges": [ ["WP_MARKT_02", "WP_MARKT_01", "worldgen"], ["WP_MARKT_02", "WP_KIRCHE_TUER"],
               ["WP_MARKT_01", "WP_MARKT_02"] ],
    "freepoints": [ { "name": "FP_SIT_BRUNNEN_01", "pos": [1.2, 3, 0.8], "dir": [0, 0, -1], "owner": "worldgen" } ]
  } })";
    const WorldFile world = parse(text);
    REQUIRE(world.waynet.has_value());
    const WaynetData& w = *world.waynet;
    // Sorted by name; dir made horizontal and unit length.
    REQUIRE(w.points.size() == 3);
    CHECK(w.points[0].name == "WP_KIRCHE_TUER");
    CHECK_FALSE(w.points[0].generated);
    CHECK(w.points[1].name == "WP_MARKT_01");
    CHECK(w.points[1].generated);
    REQUIRE(w.points[1].dir.has_value());
    CHECK(w.points[1].dir->y == 0.0f);
    CHECK(w.points[1].dir->z == doctest::Approx(1.0f));
    CHECK(w.findPoint("WP_MARKT_02") != nullptr);
    CHECK(w.findFreepoint("FP_SIT_BRUNNEN_01") != nullptr);
    CHECK(freepointType("FP_SIT_BRUNNEN_01") == "SIT");
    CHECK(freepointType("WP_MARKT_01").empty());
    // Edges: the smaller name first, duplicates merged (hand-made wins over generated).
    REQUIRE(w.edges.size() == 2);
    CHECK(w.edges[0].a == "WP_KIRCHE_TUER");
    CHECK(w.edges[0].b == "WP_MARKT_02");
    CHECK(w.edges[1].a == "WP_MARKT_01");
    CHECK_FALSE(w.edges[1].generated);

    // One entry per line; the same world gives the same bytes.
    const std::string out = writeWorldFile(world);
    CHECK(
        out.find(
            R"(      {"name":"WP_MARKT_01","pos":[12.5,3.1,-40.0],"dir":[0.0,0.0,1.0],"owner":"worldgen"})") !=
        std::string::npos);
    CHECK(out.find(R"(      ["WP_KIRCHE_TUER","WP_MARKT_02"])") != std::string::npos);
    CHECK(out.find(R"(      {"name":"FP_SIT_BRUNNEN_01")") != std::string::npos);
    CHECK(writeWorldFile(parse(out)) == out);
    // Without a waynet block nothing is written.
    CHECK(writeWorldFile(parse(R"({ "version": 1, "vobs": [] })")).find("waynet") == std::string::npos);
}

TEST_CASE("WorldFile: waynet errors name the entry")
{
    const auto bad = [](const char* waynet, const char* expected)
    {
        const std::string text = std::string(R"({ "version": 1, "vobs": [], "waynet": )") + waynet + "}";
        const std::string message = errorOf(text);
        CHECK_MESSAGE(message.find(expected) != std::string::npos, message);
    };
    bad(R"({ "points": [ { "name": "WP_A" } ] })", "waynet.points[0]: needs 'pos'");
    bad(R"({ "points": [ { "name": "wp_a", "pos": [0,0,0] } ] })",
        "waynet.points[0]: name \"wp_a\" must start with WP_");
    bad(R"({ "freepoints": [ { "name": "WP_A", "pos": [0,0,0] } ] })", "must start with FP_");
    bad(R"({ "points": [ { "name": "WP_A", "pos": [0,0,0] }, { "name": "WP_A", "pos": [1,0,0] } ] })",
        "waynet.points[1]: name \"WP_A\" is already used by points[0]");
    bad(R"({ "points": [ { "name": "WP_A", "pos": [0,0,0] } ], "edges": [ ["WP_A", "WP_B"] ] })",
        "waynet.edges[0]: unknown point \"WP_B\"");
    bad(R"({ "points": [ { "name": "WP_A", "pos": [0,0,0] } ], "edges": [ ["WP_A", "WP_A"] ] })",
        "with itself");
    bad(R"({ "points": [ { "name": "WP_A", "pos": [0,0,0] }, { "name": "WP_B", "pos": [1,0,0] } ],
           "edges": [ [0, 1] ] })",
        "waynet.edges[0]: must be");
    bad(R"({ "points": [ { "name": "WP_A", "pos": [0,0,0], "owner": "me" } ] })", "waynet.points[0].owner");
    bad(R"({ "points": [ { "name": "WP_A", "pos": [0,0,0] } ],
           "freepoints": [ { "name": "FP_SIT_X", "pos": [0,0,0] } ], "edges": [ ["WP_A", "FP_SIT_X"] ] })",
        "unknown point \"FP_SIT_X\"");
}

TEST_CASE("WorldFile: item vobs (instance, count) round-trip and are checked")
{
    const char* text = R"({ "version": 1, "vobs": [
    { "id": 5, "type": "item", "name": "APPLES", "pos": [1, 0, 2], "components": { "item": { "instance": "it_apple", "count": 3 } } },
    { "id": 6, "type": "item", "pos": [2, 0, 2], "components": { "item": { "instance": "it_club" } } } ] })";
    const WorldFile world = parse(text);
    REQUIRE(world.vobs.size() == 2);
    CHECK(world.vobs[0].type == VobType::Item);
    CHECK(world.vobs[0].item.instance == "it_apple");
    CHECK(world.vobs[0].item.count == 3);
    CHECK(world.vobs[1].item.count == 1);
    const std::string out = writeWorldFile(world);
    CHECK(out.find(R"("components":{"item":{"instance":"it_apple","count":3}})") != std::string::npos);
    CHECK(out.find(R"("components":{"item":{"instance":"it_club"}})") !=
          std::string::npos); // count 1 left out
    CHECK(writeWorldFile(parse(out)) == out);

    CHECK(
        errorOf(R"({ "version": 1, "vobs": [ { "id": 1, "type": "item", "components": { "item": {} } } ] })")
            .find("vobs[0].components.item: needs 'instance'") != std::string::npos);
    CHECK(errorOf(R"({ "version": 1, "vobs": [ { "id": 1, "type": "item",
              "components": { "item": { "instance": "it_apple", "count": 0 } } } ] })")
              .find("components.item.count: must be a whole number") != std::string::npos);
}

TEST_CASE("WorldFile: an item vob's owner is read and written back")
{
    const WorldFile world = parse(R"({ "version": 1, "vobs": [
    { "id": 5, "type": "item", "pos": [1, 0, 2], "components": { "item": { "instance": "it_bread", "owner": "npc_farmer_woman" } } } ] })");
    CHECK(world.vobs[0].item.owner == "npc_farmer_woman");
    const std::string out = writeWorldFile(world);
    CHECK(out.find(R"("item":{"instance":"it_bread","owner":"npc_farmer_woman"})") != std::string::npos);
    CHECK(
        errorOf(
            R"({ "version": 1, "vobs": [ { "id": 1, "type": "item", "components": { "item": { "instance": "it_bread", "owner": 3 } } } ] })")
            .find("'owner' must be a string") != std::string::npos);
}

TEST_CASE("WorldFile: numbers are written with the fewest digits, as Python writes them")
{
    // welt's generator (Python json) and an editor save must agree byte for byte: the shortest digits that
    // read back to the same double, fixed for exponents -4..15, otherwise "1e-05". Expected: Python's
    // json.dumps.
    constexpr std::string_view kNumbers =
        R"([{"type":"numbers","v":[24.12317,24.123169999999998,0.1,0.0001,1e-05,-1.5e-07,4.0,-0.5,123456.78901,)"
        R"(1e15,1e16,1.7976931348623157e308,5e-324,0.6666666666666666,-0.0,0.30000000000000004,)"
        R"(1234567890123456.0,9.999999999999999e-05]}])";
    constexpr std::string_view kPython =
        R"([{"type":"numbers","v":[24.12317,24.12317,0.1,0.0001,1e-05,-1.5e-07,4.0,-0.5,123456.78901,)"
        R"(1000000000000000.0,1e+16,1.7976931348623157e+308,5e-324,0.6666666666666666,-0.0,)"
        R"(0.30000000000000004,1234567890123456.0,9.999999999999999e-05]}])";
    WorldFile world = parse(kCamp);
    world.zones = {Zone{"numbers", "", std::nullopt, std::string(kNumbers).substr(1, kNumbers.size() - 2)}};
    world.waynet->points[0].position = Vec3(24.12317f, 0.0001f, -0.00001f);
    const std::string text = writeWorldFile(world);
    CHECK(text.find(std::string(kPython).substr(1, kPython.size() - 2)) != std::string::npos);
    CHECK(text.find(R"("pos":[24.12317,0.0001,-1e-05])") != std::string::npos);
    CHECK(writeWorldFile(parse(text)) == text);
}

TEST_CASE("WorldFile: positions and rotations come back as welt's generator wrote them")
{
    // Leonberg: an engine save must equal the generated file. -343.106 is the float -343.10598755 (rounding
    // it to 1e-5 gave -343.10599); a quaternion with six decimals is not exactly unit length (normalizing
    // changed it).
    constexpr std::string_view kWorld = R"({
  "version": 1,
  "name": "w",
  "nextVobId": 3,
  "vobs": [
    {"id":1,"type":"mesh","name":"BLD","pos":[181.715,23.39,-343.106],"rot":[0.0,0.169246,0.0,0.985574],"mesh":"m.glb"},
    {"id":2,"type":"empty","name":"NOISE","pos":[-0.0000000437,0.70710677,1000.25]}
  ]
})";
    const std::string text = writeWorldFile(parse(kWorld));
    CHECK(text.find(R"("pos":[181.715,23.39,-343.106],"rot":[0.0,0.169246,0.0,0.985574])") !=
          std::string::npos);
    // Arithmetic noise (more than six decimals) is rounded to 1e-5, no -0.
    CHECK(text.find(R"("pos":[0.0,0.70711,1000.25])") != std::string::npos);
    CHECK(writeWorldFile(parse(text)) == text);
    // A quaternion clearly off unit length is still normalized.
    WorldFile scaled = parse(
        R"({"version":1,"name":"w","nextVobId":2,"vobs":[{"id":1,"type":"empty","name":"Q","rot":[0,0,0,2]}]})");
    CHECK(scaled.vobs[0].transform.rotation.w == doctest::Approx(1.0f));
}

TEST_CASE("WorldFile: indoor zones - turned boxes read, checked, written one per line in order")
{
    // welt's rooms: several boxes per room (a pentagonal smithy), sorted by value, then box centre x, then z.
    WorldFile world = parse(kCamp);
    std::string text = writeWorldFile(world);
    const std::string zones =
        R"([{"type":"indoor","value":"LEO_SCHMIEDE_ZNP_INNEN","box":{"center":[5.0,1.2,1.0],"halfExtents":[2.0,1.2,1.5],"yaw":30.0}},)"
        R"({"type":"music","value":"CAMP"},)"
        R"({"type":"indoor","value":"LEO_SCHMIEDE_ZNP_INNEN","box":{"center":[2.0,1.2,4.0],"halfExtents":[1.0,1.2,1.0],"yaw":30.0}},)"
        R"({"type":"indoor","value":"LEO_GASTHAUS_ZHE_INNEN","box":{"center":[-90.5,0.1,28.0],"halfExtents":[3.1,1.45,2.6],"yaw":-71.86}}])";
    const auto at = text.find("\"zones\":");
    REQUIRE(at != std::string::npos);
    const auto end = text.find("\n}", at);
    const std::string base = text;
    text = base.substr(0, at) + "\"zones\": " + zones + base.substr(end);
    const WorldFile read = parse(text);
    REQUIRE(read.zones.size() == 4);
    REQUIRE(read.zones[0].box.has_value());
    CHECK(read.zones[0].box->yawDegrees == doctest::Approx(30.0f));
    CHECK(read.zones[0].box->halfExtents == Vec3(2.0f, 1.2f, 1.5f));
    const std::string written = writeWorldFile(read);
    const auto gasthaus = written.find("LEO_GASTHAUS_ZHE_INNEN");
    const auto smithyWest = written.find(R"("center":[2.0,1.2,4.0])");
    const auto smithyEast = written.find(R"("center":[5.0,1.2,1.0])");
    CHECK(written.find(R"({"type":"music","value":"CAMP"})") != std::string::npos);
    CHECK(gasthaus < smithyWest);
    CHECK(smithyWest < smithyEast); // same room: by x
    CHECK(
        written.find(
            R"({"type":"indoor","value":"LEO_GASTHAUS_ZHE_INNEN","box":{"center":[-90.5,0.1,28.0],"halfExtents":[3.1,1.45,2.6],"yaw":-71.86}})") !=
        std::string::npos);
    CHECK(writeWorldFile(parse(written)) == written); // stable

    const auto fails = [&](const char* zone, const char* expected)
    {
        std::string bad = base.substr(0, at) + "\"zones\": [" + zone + "]" + base.substr(end);
        auto r = parseWorldFile(bad, "w.g7world");
        REQUIRE_FALSE(r.ok());
        CHECK_MESSAGE(r.error().message.find(expected) != std::string::npos, r.error().message);
    };
    fails(R"({"type":"indoor","box":{"center":[0,0,0],"halfExtents":[1,1,1]}})", "zones[0]");
    fails(R"({"type":"indoor","value":"R"})", "box");
    fails(R"({"type":"indoor","value":"R","box":{"center":[0,0,0],"halfExtents":[1,0,1]}})", "halfExtents");
    fails(R"({"value":"R"})", "type");
}

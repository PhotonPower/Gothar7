// Zone editing (M16 part C): the gizmo's view of a box (yaw about +Y as world.md defines it), the smallest
// zone at a point, picking, value lists, undo through the history, and saving byte for byte.

#include <g7/editor/Commands.hpp>
#include <g7/editor/ZoneEdit.hpp>
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
const char* const kWorld = R"({"version": 1, "nextVobId": 2, "vobs": [
  {"id": 1, "type": "start", "name": "START", "pos": [0, 0, 0]}
], "zones": [
  {"type": "music", "value": "WALD", "box": {"center": [0, 0, 0], "halfExtents": [100, 50, 100]}},
  {"type": "music", "value": "LAGER", "box": {"center": [10, 0, 0], "halfExtents": [20, 10, 20], "yaw": 30}},
  {"type": "ambient", "value": "camp", "box": {"center": [10, 0, 0], "halfExtents": [20, 10, 20]}},
  {"type": "indoor", "value": "LEO_SCHMIEDE_S1_INNEN", "box": {"center": [12, 1.5, 2], "halfExtents": [2, 1.5, 3], "yaw": 90}},
  {"type": "fog", "value": "dense", "density": 0.3}
]})";

world::WorldFile load()
{
    auto world = world::parseWorldFile(kWorld, "w.g7world");
    REQUIRE(world.ok());
    return std::move(world).value();
}
} // namespace

TEST_CASE("Zone edit: the gizmo transform keeps world.md's yaw; only the yaw of a rotation counts")
{
    const world::ZoneBox box{Vec3(12, 1.5f, 2), Vec3(2, 1.5f, 3), 90.0f};
    const Transform t = zoneTransform(box);
    // yaw 90: local +X = world -Z, local +Z = world +X (world.md example).
    const Vec3 x = t.rotation * Vec3(1, 0, 0);
    const Vec3 z = t.rotation * Vec3(0, 0, 1);
    CHECK(x.z == doctest::Approx(-1.0f));
    CHECK(z.x == doctest::Approx(1.0f));
    CHECK(world::zoneContains(box, Vec3(12 + 2.9f, 1.5f, 2))); // world X reaches +-3
    CHECK_FALSE(world::zoneContains(box, Vec3(12, 1.5f, 2 + 2.9f)));
    const world::ZoneBox back = zoneBoxFrom(t);
    CHECK(back.yawDegrees == doctest::Approx(90.0f));
    CHECK(back.center == box.center);
    CHECK(back.halfExtents == box.halfExtents);
    // A tilt is dropped, the size kept at least 5 cm.
    Transform tilted = t;
    tilted.rotation =
        glm::angleAxis(glm::radians(-45.0f), Vec3(0, 1, 0)) * glm::angleAxis(0.3f, Vec3(1, 0, 0));
    tilted.scale = Vec3(0.0f, 2.0f, 1.0f);
    const world::ZoneBox flat = zoneBoxFrom(tilted);
    CHECK(flat.yawDegrees == doctest::Approx(-45.0f));
    CHECK(flat.halfExtents.x == doctest::Approx(0.05f));
}

TEST_CASE("Zone edit: the smallest zone counts; picking; value lists; labels")
{
    const world::WorldFile w = load();
    const std::vector<world::Zone>& zones = w.zones;
    const auto at = [&](std::string_view type, const Vec3& p)
    {
        const auto i = smallestZoneIndex(zones, type, p);
        return i ? zones[*i].value : std::string("-");
    };
    CHECK(at("music", Vec3(10, 0, 0)) == "LAGER"); // the camp inside the forest
    CHECK(at("music", Vec3(-80, 0, 0)) == "WALD");
    CHECK(at("music", Vec3(500, 0, 0)) == "-");
    CHECK(at("indoor", Vec3(12, 1, 2)) == "LEO_SCHMIEDE_S1_INNEN");
    CHECK(at("ambient", Vec3(10, 0, 0)) == "camp");

    // From outside: the box entered first. From inside everything: the smallest around the eye.
    const auto pick = [&](const Vec3& from, const Vec3& to)
    {
        const auto i = pickZone(zones, Ray{from, glm::normalize(to - from)});
        return i ? zones[*i].value : std::string("-");
    };
    CHECK(pick(Vec3(12, 1.5f, 50), Vec3(12, 1.5f, 2)).starts_with("LEO_") ==
          false); // the camp boxes come first
    CHECK(pick(Vec3(12, 1.5f, 2.5f), Vec3(12, 1.5f, -50)) != "-");
    CHECK(pick(Vec3(12, 1, 2), Vec3(12, 1, 3)) == "LEO_SCHMIEDE_S1_INNEN"); // inside all: the smallest
    CHECK(pick(Vec3(0, 500, 0), Vec3(0, 600, 0)) == "-");

    const std::vector<std::string> themes{"LAGER", "STADT", "common"};
    const std::vector<std::string> amb{"wald", "camp"};
    CHECK(zoneValues("music", zones, themes, amb) == std::vector<std::string>{"LAGER", "STADT"});
    CHECK(zoneValues("ambient", zones, themes, amb) == std::vector<std::string>{"camp", "wald"});
    CHECK(zoneValues("indoor", zones, themes, amb) == std::vector<std::string>{"LEO_SCHMIEDE_S1_INNEN"});
    CHECK(editableZone(zones[0]));
    CHECK_FALSE(editableZone(zones[4])); // "fog": shown, kept
    CHECK(zoneLabel(zones[1]) == "music LAGER (40 x 20 x 40 m)");
    const world::Zone room = newZone("indoor", "LEO_X_Y_INNEN", Vec3(1, 2, 3));
    CHECK(room.box->halfExtents == Vec3(3.0f, 1.5f, 3.0f));
}

TEST_CASE("Zone edit: undo and redo through the history; saving gives the same bytes")
{
    world::WorldFile w = load();
    const std::string original = world::writeWorldFile(w);
    CHECK(world::writeWorldFile(load()) == original); // same world, same bytes
    // Read again and written: the same bytes (sorted by value, the unknown type kept).
    auto again = world::parseWorldFile(original, "again");
    REQUIRE(again.ok());
    CHECK(world::writeWorldFile(again.value()) == original);
    CHECK(again.value().zones.size() == w.zones.size());

    History history;
    world::Scene scene;
    std::vector<world::Zone> zones = w.zones;
    // Move the camp's music box, then add a zone - two commands as the editor records them.
    Command move{"zone LAGER box", {}, {}, true};
    move.zonesBefore = zones;
    zones[1].box->center.x += 5.0f;
    move.zonesAfter = zones;
    history.record(std::move(move));
    Command typing{"zone LAGER box", {}, {}, true}; // the next drag step of the same field joins it
    typing.zonesBefore = zones;
    zones[1].box->halfExtents.x = 25.0f;
    typing.zonesAfter = zones;
    history.record(std::move(typing));
    CHECK(history.size() == 1);
    Command add{"add music zone STADT", {}, {}, false};
    add.zonesBefore = zones;
    zones.push_back(newZone("music", "STADT", Vec3(300, 0, 0)));
    add.zonesAfter = zones;
    history.record(std::move(add));
    const std::vector<world::Zone> edited = zones;

    REQUIRE(history.undo(scene, nullptr, &zones).value());
    CHECK(zones.size() == w.zones.size());
    REQUIRE(history.undo(scene, nullptr, &zones).value());
    CHECK(zones == w.zones);
    w.zones = zones;
    CHECK(world::writeWorldFile(w) == original); // undone: byte for byte the file it was
    REQUIRE(history.redo(scene, nullptr, &zones).value());
    REQUIRE(history.redo(scene, nullptr, &zones).value());
    CHECK(zones == edited);
    for (usize i = 0; i < zones.size(); ++i)
    {
        CHECK(world::checkZone(zones[i], "z").ok());
    }
    world::Zone broken = zones[0];
    broken.box->halfExtents.y = 0.0f;
    CHECK_FALSE(world::checkZone(broken, "zones[0]").ok());
    broken.value.clear();
    CHECK(world::checkZone(broken, "zones[0]").error().message.find("value") != std::string::npos);
}

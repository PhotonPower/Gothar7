// Teleporters and falls in .g7world (docs/modules/dungeon.md D4): components.trigger.teleport and
// changeWorld.fall, read and written byte-equal.

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
WorldFile parse(std::string_view text)
{
    auto world = parseWorldFile(text, "teleport.g7world");
    REQUIRE_MESSAGE(world.ok(), (world.ok() ? "" : world.error().message));
    return std::move(world).value();
}

std::string errorOf(std::string_view vob)
{
    auto world =
        parseWorldFile(std::string(R"({"version": 1, "vobs": [)") + std::string(vob) + "]}", "w.g7world");
    REQUIRE_FALSE(world.ok());
    return world.error().message;
}
} // namespace

TEST_CASE("Teleport format: within the world or to another level, falls, written byte-equal")
{
    const WorldFile file = parse(R"({"version": 1, "name": "t", "vobs": [
  {"id": 1, "type": "trigger", "name": "TELE_E01_1",
   "components": {"trigger": {"teleport": {"to": "MARK", "visible": false, "filter": "any", "yaw": 90}}}},
  {"id": 2, "type": "trigger", "name": "TELE_E01_2",
   "components": {"trigger": {"teleport": {"world": "dungeon_2.g7world", "start": "START_E02", "active": false}}}},
  {"id": 3, "type": "trigger", "name": "FALL",
   "components": {"trigger": {"changeWorld": {"world": "dungeon_2.g7world", "start": "START_E02_SCHACHT", "fall": true}}}}
]})");
    REQUIRE(file.vobs[0].trigger.teleport.has_value());
    const Teleport& near = *file.vobs[0].trigger.teleport;
    CHECK(near.to == "MARK");
    CHECK_FALSE(near.visible);
    CHECK(near.filter == Teleport::Filter::Any);
    CHECK(*near.yaw == 90.0f);
    CHECK(near.active);
    const Teleport& far = *file.vobs[1].trigger.teleport;
    CHECK(far.world == "dungeon_2.g7world");
    CHECK(far.start == "START_E02");
    CHECK(far.visible); // the default
    CHECK(far.filter == Teleport::Filter::Player);
    CHECK_FALSE(far.active);
    CHECK(file.vobs[2].trigger.changeFall);

    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    const std::string written = writeWorldFile(captureWorld(scene, "t"));
    CHECK(writeWorldFile(parse(written)) == written);
    CHECK(
        written.find(R"("teleport":{"to":"MARK","visible":false,"filter":"any","yaw":90.0,"active":true})") !=
        std::string::npos);
    CHECK(written.find(R"("teleport":{"world":"dungeon_2.g7world","start":"START_E02","visible":true,)"
                       R"("filter":"player","active":false})") != std::string::npos);
    CHECK(written.find(
              R"("changeWorld":{"world":"dungeon_2.g7world","start":"START_E02_SCHACHT","fall":true})") !=
          std::string::npos);
}

TEST_CASE("Teleport format: a teleporter needs one target")
{
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"teleport": {}}}})") ==
          "w.g7world: vobs[0].components.trigger.teleport: needs 'to' (a vob of this world) or 'world' and "
          "'start'");
    CHECK(errorOf(R"({"id": 1, "type": "trigger",
                      "components": {"trigger": {"teleport": {"to": "A", "world": "b", "start": "C"}}}})") ==
          "w.g7world: vobs[0].components.trigger.teleport: needs 'to' (a vob of this world) or 'world' and "
          "'start'");
    CHECK(
        errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"teleport": {"world": "b"}}}})") ==
        "w.g7world: vobs[0].components.trigger.teleport: needs 'to' (a vob of this world) or 'world' and "
        "'start'");
    CHECK(errorOf(R"({"id": 1, "type": "trigger",
                      "components": {"trigger": {"teleport": {"to": "A", "filter": "ghosts"}}}})") ==
          "w.g7world: vobs[0].components.trigger.teleport: 'filter' must be one of 'player', 'creature', "
          "'items', "
          "'any'");
}

TEST_CASE("Teleport format: silent and keep_velocity, written after active only when set, round trip")
{
    const WorldFile file = parse(R"({"version": 1, "name": "t", "vobs": [
  {"id": 1, "type": "trigger", "name": "KLAPPE",
   "components": {"trigger": {"teleport": {"to": "MARK", "visible": false, "active": true, "silent": true,
                                           "keep_velocity": true}}}},
  {"id": 2, "type": "trigger", "name": "PLAIN", "components": {"trigger": {"teleport": {"to": "MARK"}}}}
]})");
    const Teleport& trap = *file.vobs[0].trigger.teleport;
    CHECK(trap.silent);
    CHECK(trap.keepVelocity);
    const Teleport& plain = *file.vobs[1].trigger.teleport;
    CHECK_FALSE(plain.silent); // the defaults
    CHECK_FALSE(plain.keepVelocity);

    // Read -> write -> read: the editor's save keeps both fields, in welt's order after active.
    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    const std::string written = writeWorldFile(captureWorld(scene, "t"));
    CHECK(written.find(R"("teleport":{"to":"MARK","visible":false,"filter":"player","active":true,)"
                       R"("silent":true,"keep_velocity":true})") != std::string::npos);
    CHECK(written.find(R"("teleport":{"to":"MARK","visible":true,"filter":"player","active":true})") !=
          std::string::npos);
    const WorldFile again = parse(written);
    CHECK(*again.vobs[0].trigger.teleport == trap);
    CHECK(*again.vobs[1].trigger.teleport == plain);
    CHECK(writeWorldFile(again) == written);

    CHECK(errorOf(R"({"id": 1, "type": "trigger",
                      "components": {"trigger": {"teleport": {"to": "A", "silent": 1}}}})") ==
          "w.g7world: vobs[0].components.trigger.teleport.silent: must be true or false");
}

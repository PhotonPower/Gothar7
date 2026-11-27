// Spawn markers (docs/modules/dungeon.md D6): the vob type `spawn` in .g7world.

#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>
#include <g7/world/WorldScene.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

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
} // namespace

TEST_CASE("Spawn format: spawn vobs are read, spawned, captured and written unchanged")
{
    auto parsed = parseWorldFile(R"({"version": 1, "name": "spawns", "vobs": [
  {"id": 1, "type": "spawn", "name": "SPAWN_NEST", "pos": [1, 0, 2],
   "components": {"spawn": {"npc": "mon_mauerkriecher", "count": 3, "respawn": true, "dormant": false}}},
  {"id": 2, "type": "spawn", "name": "SPAWN_WACHE", "components": {"spawn": {"npc": "mon_gatterbock"}}}
]})",
                                 "spawns.g7world");
    REQUIRE_MESSAGE(parsed.ok(), (parsed.ok() ? "" : parsed.error().message));
    const WorldFile& file = parsed.value();
    REQUIRE(file.vobs.size() == 2);
    REQUIRE(file.vobs[0].type == VobType::Spawn);
    CHECK(file.vobs[0].spawn.npc == "mon_mauerkriecher");
    CHECK(file.vobs[0].spawn.count == 3);
    CHECK(file.vobs[0].spawn.respawn);
    CHECK_FALSE(file.vobs[0].spawn.dormant);
    const Spawn& plain = file.vobs[1].spawn; // the defaults: one, resting, no nest
    CHECK(plain.count == 1);
    CHECK_FALSE(plain.respawn);
    CHECK(plain.dormant);

    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    CHECK(scene.get<Spawn>(scene.findById(VobId{1}))->count == 3);
    const std::string written = writeWorldFile(captureWorld(scene, "spawns"));
    auto again = parseWorldFile(written, "again.g7world");
    REQUIRE(again.ok());
    CHECK(writeWorldFile(again.value()) == written);
    CHECK(written.find(R"("components":{"spawn":{"npc":"mon_gatterbock","count":1,"respawn":false,)"
                       R"("dormant":true}})") != std::string::npos);
}

TEST_CASE("Spawn format: a spawn needs its Npc and a count from 1 to 32")
{
    CHECK(errorOf(R"({"id": 1, "type": "spawn"})").find("npc") != std::string::npos);
    CHECK(errorOf(R"({"id": 1, "type": "spawn", "components": {"spawn": {"npc": "mon_x", "count": 0}}})") ==
          "w.g7world: vobs[0].components.spawn.count: must be a whole number from 1 to 32");
    CHECK(errorOf(R"({"id": 1, "type": "spawn", "components": {"spawn": {"npc": "mon_x", "respawn": 1}}})") ==
          "w.g7world: vobs[0].components.spawn.respawn: must be true or false");
}

TEST_CASE("Spawn format: targets, action and active (E8), written only when set, round trip")
{
    auto parsed = parseWorldFile(R"({"version": 1, "name": "boss", "vobs": [
  {"id": 1, "type": "spawn", "name": "SPAWN_BOSS",
   "components": {"spawn": {"npc": "mon_kellermutter", "targets": ["RELAY_BOSS_TOT"], "action": "on"}}},
  {"id": 2, "type": "spawn", "name": "SPAWN_WELLE",
   "components": {"spawn": {"npc": "mon_mauerkriecher", "count": 3, "dormant": false, "active": false}}},
  {"id": 3, "type": "spawn", "name": "SPAWN_PLAIN", "components": {"spawn": {"npc": "mon_gatterbock"}}}
]})",
                                 "boss.g7world");
    REQUIRE_MESSAGE(parsed.ok(), (parsed.ok() ? "" : parsed.error().message));
    const WorldFile& file = parsed.value();
    CHECK(file.vobs[0].spawn.targets == std::vector<std::string>{"RELAY_BOSS_TOT"});
    CHECK(file.vobs[0].spawn.action == "on");
    CHECK(file.vobs[0].spawn.active);
    CHECK_FALSE(file.vobs[1].spawn.active);
    CHECK(file.vobs[1].spawn.targets.empty());
    CHECK(file.vobs[2].spawn.action == "trigger"); // the default
    CHECK(file.vobs[2].spawn.active);

    // Read -> write -> read: the editor's save keeps them, after dormant.
    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    const std::string written = writeWorldFile(captureWorld(scene, "boss"));
    CHECK(written.find(R"("spawn":{"npc":"mon_kellermutter","count":1,"respawn":false,"dormant":true,)"
                       R"("targets":["RELAY_BOSS_TOT"],"action":"on"})") != std::string::npos);
    CHECK(written.find(R"("spawn":{"npc":"mon_mauerkriecher","count":3,"respawn":false,"dormant":false,)"
                       R"("active":false})") != std::string::npos);
    CHECK(written.find(R"("spawn":{"npc":"mon_gatterbock","count":1,"respawn":false,"dormant":true})") !=
          std::string::npos);
    auto again = parseWorldFile(written, "again.g7world");
    REQUIRE(again.ok());
    CHECK(again.value().vobs[0].spawn.targets == file.vobs[0].spawn.targets);
    CHECK_FALSE(again.value().vobs[1].spawn.active);
    CHECK(writeWorldFile(again.value()) == written);

    CHECK(errorOf(R"({"id": 1, "type": "spawn", "components": {"spawn": {"npc": "a", "active": 0}}})")
              .find("active") != std::string::npos);
    CHECK(errorOf(R"({"id": 1, "type": "spawn", "components": {"spawn": {"npc": "a", "action": "dance"}}})")
              .find("action") != std::string::npos);
}

// Switches and plates in .g7world (docs/modules/dungeon.md D3): components.mob targets/action/once/hidden and
// components.trigger.plate, read and written byte-equal.

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
WorldFile parse(std::string_view text)
{
    auto world = parseWorldFile(text, "switches.g7world");
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

TEST_CASE("Switch format: levers and buttons name targets, plates weigh, written byte-equal")
{
    const WorldFile file = parse(R"({"version": 1, "name": "s", "vobs": [
  {"id": 1, "type": "mob", "name": "LEVER_SCHLOSS_FASS", "mesh": "a.glb",
   "components": {"mob": {"definition": "lever", "targets": ["WALL_SCHLOSS_FASS"], "action": "open", "hidden": true}}},
  {"id": 2, "type": "mob", "name": "BUTTON_E01_1", "mesh": "b.glb",
   "components": {"mob": {"definition": "wall_button", "open": false, "targets": ["NICHE"], "once": true}}},
  {"id": 3, "type": "trigger", "name": "PLATE_E01_1",
   "components": {"trigger": {"targets": ["GATE_E01_1"], "action": "open", "plate": {"weight": "creature"}}}},
  {"id": 4, "type": "trigger", "name": "PLATE_E01_2",
   "components": {"trigger": {"targets": ["RELAY"], "action": "on", "plate": {"release_action": "trigger"}}}},
  {"id": 5, "type": "mob", "name": "CHEST", "mesh": "c.glb", "components": {"mob": {"definition": "chest"}}}
]})");
    const MobRef& lever = file.vobs[0].mob;
    CHECK(lever.targets == std::vector<std::string>{"WALL_SCHLOSS_FASS"});
    CHECK(lever.action == "open");
    CHECK(lever.hidden);
    CHECK_FALSE(lever.once);
    const MobRef& button = file.vobs[1].mob;
    CHECK(button.once);
    CHECK(button.action == "trigger"); // the default
    REQUIRE(file.vobs[2].trigger.plate.has_value());
    CHECK(file.vobs[2].trigger.plate->weight == Plate::Weight::Creature);
    CHECK(file.vobs[2].trigger.plate->releaseAction.empty());
    CHECK(file.vobs[3].trigger.plate->weight == Plate::Weight::Any);
    CHECK(file.vobs[3].trigger.plate->releaseAction == "trigger");
    CHECK_FALSE(file.vobs[4].mob.hidden);

    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    const std::string written = writeWorldFile(captureWorld(scene, "s"));
    CHECK(writeWorldFile(parse(written)) == written);
    CHECK(written.find(R"("mob":{"definition":"lever","targets":["WALL_SCHLOSS_FASS"],"action":"open",)"
                       R"("hidden":true})") != std::string::npos);
    CHECK(written.find(
              R"("mob":{"definition":"wall_button","targets":["NICHE"],"action":"trigger","once":true})") !=
          std::string::npos);
    CHECK(written.find(R"("targets":["GATE_E01_1"],"action":"open","plate":{"weight":"creature"}})") !=
          std::string::npos);
    CHECK(written.find(R"("plate":{"weight":"any","release_action":"trigger"})") != std::string::npos);
    CHECK(written.find(R"("mob":{"definition":"chest"}})") != std::string::npos); // others stay as they were
}

TEST_CASE("Switch format: broken switches and plates are errors naming the entry")
{
    CHECK(errorOf(R"({"id": 1, "type": "mob", "mesh": "a.glb",
                      "components": {"mob": {"definition": "lever", "action": "pull"}}})")
              .find("vobs[0].components.mob: 'action' must be one of") != std::string::npos);
    CHECK(errorOf(R"({"id": 1, "type": "mob", "mesh": "a.glb",
                      "components": {"mob": {"definition": "lever", "hidden": 1}}})") ==
          "w.g7world: vobs[0].components.mob.hidden: must be true or false");
    CHECK(errorOf(
              R"({"id": 1, "type": "trigger", "components": {"trigger": {"plate": {"weight": "heavy"}}}})") ==
          "w.g7world: vobs[0].components.trigger.plate: 'weight' must be one of 'any', 'creature', 'player'");
    CHECK(
        errorOf(
            R"({"id": 1, "type": "trigger", "components": {"trigger": {"plate": {"release_action": "x"}}}})") ==
        "w.g7world: vobs[0].components.trigger.plate: 'release_action' must be an action like 'action'");
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"plate": true}}})") ==
          "w.g7world: vobs[0].components.trigger.plate: must be an object");
}

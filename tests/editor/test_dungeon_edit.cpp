// Dungeon parts in the editor (dungeon D7): fields from Blender's extras on VOB_ nodes and mesh nodes, the
// wiring as lines and its checks.

#include <g7/editor/DungeonEdit.hpp>
#include <g7/editor/GlbVobImport.hpp>
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
bool mentions(const std::vector<std::string>& warnings, std::string_view text)
{
    return std::ranges::any_of(warnings,
                               [&](const std::string& w) { return w.find(text) != std::string::npos; });
}
} // namespace

TEST_CASE("Dungeon edit: names from one text and back")
{
    CHECK(splitNames(" GATE_A, GATE_B;;RELAY_1 ,") ==
          std::vector<std::string>{"GATE_A", "GATE_B", "RELAY_1"});
    CHECK(splitNames("").empty());
    const std::vector<std::string> names{"A", "B"};
    CHECK(joinNames(names) == "A, B");
    CHECK(splitNames(joinNames(names)) == names);
}

TEST_CASE("Blender vobs: extras of plates, teleporters, relays, spawn markers and triggers")
{
    const Mat4 at =
        glm::translate(Mat4(1.0f), Vec3(1, 0, 2)) * glm::scale(Mat4(1.0f), Vec3(0.8f, 0.2f, 0.8f));
    const std::vector<VobMarker> markers{
        {"VOB_PLATE_Stein", at,
         R"({"targets": "GATE_A, GATE_B", "action": "open", "weight": "any", "release_action": "close",
             "once": 1})"},
        {"VOB_TELEPORT_Rueck", Mat4(1.0f), R"({"to": "START_OBEN", "visible": 0, "filter": "creature"})"},
        {"VOB_TELEPORT_Ebene", Mat4(1.0f), R"({"world": "dungeon_2.g7world", "start": "START_E02"})"},
        {"VOB_RELAY_Beide", Mat4(1.0f),
         R"({"mode": "count", "count": 2.0, "invert": 1, "delay": 0.5, "targets": ["GATE_A"], "action": "close"})"},
        {"VOB_SPAWN_Bock", Mat4(1.0f),
         R"({"npc": "mon_gatterbock", "count": 3, "dormant": 0, "colour": "red"})"},
        {"VOB_TRIGGER_Schacht", Mat4(1.0f),
         R"({"world": "dungeon_2.g7world", "start": "START_UNTEN", "fall": 1})"},
        {"VOB_LIGHT_Fackel", Mat4(1.0f), R"({"color": [1.0, 0.5, 0.2], "flicker": 0.4})"},
        {"VOB_RELAY_Kaputt", Mat4(1.0f), R"({"action": "explode"})"},
        {"VOB_START_Oben", Mat4(1.0f), R"({"targets": "X"})"},
    };
    const VobImport imported = vobsFromMarkers(markers);
    REQUIRE(imported.vobs.size() == 8); // the broken relay is skipped

    const world::WorldFileVob& plate = imported.vobs[0];
    CHECK(plate.type == world::VobType::Trigger);
    REQUIRE(plate.trigger.plate.has_value());
    CHECK(plate.trigger.targets == std::vector<std::string>{"GATE_A", "GATE_B"});
    CHECK(plate.trigger.action == "open");
    CHECK(plate.trigger.plate->releaseAction == "close");
    CHECK(plate.trigger.once);
    CHECK(plate.trigger.halfExtents.y == doctest::Approx(0.2f)); // the empty's size stays the volume
    CHECK(plate.transform.position == Vec3(1, 0, 2));

    const world::WorldFileVob& back = imported.vobs[1];
    REQUIRE(back.trigger.teleport.has_value());
    CHECK(back.trigger.teleport->to == "START_OBEN");
    CHECK_FALSE(back.trigger.teleport->visible);
    CHECK(back.trigger.teleport->filter == world::Teleport::Filter::Creature);
    REQUIRE(imported.vobs[2].trigger.teleport.has_value());
    CHECK(imported.vobs[2].trigger.teleport->start == "START_E02");

    const world::WorldFileVob& relay = imported.vobs[3];
    CHECK(relay.type == world::VobType::Relay);
    CHECK(relay.relay.mode == world::Relay::Mode::Count);
    CHECK(relay.relay.count == 2);
    CHECK(relay.relay.invert);
    CHECK(relay.relay.delay == doctest::Approx(0.5f));
    CHECK(relay.relay.action == "close");

    const world::WorldFileVob& spawn = imported.vobs[4];
    CHECK(spawn.type == world::VobType::Spawn);
    CHECK(spawn.name == "BOCK");
    CHECK(spawn.spawn.npc == "mon_gatterbock");
    CHECK(spawn.spawn.count == 3);
    CHECK_FALSE(spawn.spawn.dormant);

    const world::WorldFileVob& shaft = imported.vobs[5];
    CHECK(shaft.trigger.changeWorld == "dungeon_2.g7world");
    CHECK(shaft.trigger.changeStart == "START_UNTEN");
    CHECK(shaft.trigger.changeFall);

    CHECK(imported.vobs[6].light.color == Vec3(1.0f, 0.5f, 0.2f));
    CHECK(imported.vobs[6].light.flicker == doctest::Approx(0.4f));
    CHECK(imported.vobs[7].type == world::VobType::Start);

    CHECK(mentions(imported.warnings, "'colour'"));         // unknown key, left out
    CHECK(mentions(imported.warnings, "VOB_RELAY_Kaputt")); // wrong action: skipped
    CHECK(mentions(imported.warnings, "takes no extras"));  // a start point
}

TEST_CASE("Blender vobs: extras.mover of a mesh node")
{
    std::vector<std::string> warnings;
    const auto gate =
        moverFromExtras(R"({"mover": {"kind": "gate", "seconds": 1.5, "open": 1, "crush": "damage",
                                                   "sound_move": "gate_up"}})",
                        "Gitter", warnings);
    REQUIRE(gate.has_value());
    CHECK(gate->kind == world::Mover::Kind::Gate);
    CHECK(gate->seconds == doctest::Approx(1.5f));
    CHECK(gate->open);
    CHECK(gate->crush == world::Mover::Crush::Damage);
    CHECK(gate->soundMove == "gate_up");
    const auto trapdoor = moverFromExtras(R"({"mover": "trapdoor"})", "Klappe", warnings);
    REQUIRE(trapdoor.has_value());
    CHECK(trapdoor->kind == world::Mover::Kind::Trapdoor);
    CHECK(trapdoor->seconds == doctest::Approx(world::kMoverSeconds[2]));
    CHECK(warnings.empty());
    CHECK_FALSE(moverFromExtras(R"({"mover": "drawbridge"})", "Bruecke", warnings).has_value());
    CHECK(mentions(warnings, "Bruecke"));
    CHECK_FALSE(moverFromExtras(R"({"wind": "leaves"})", "Baum", warnings).has_value()); // no mover
}

TEST_CASE("Dungeon edit: lines of the wiring and the checks")
{
    auto file = world::parseWorldFile(R"({"version": 1, "nextVobId": 20, "vobs": [
  {"id": 1, "type": "start", "name": "START_OBEN", "pos": [0, 0, 0]},
  {"id": 2, "type": "mesh", "name": "GATE_A", "pos": [0, 0, 6], "mesh": "testworld/block.gltf",
   "components": {"mover": {"kind": "gate"}}},
  {"id": 3, "type": "mob", "name": "LEVER_A", "pos": [3, 0, 0], "mesh": "testworld/block.gltf",
   "components": {"mob": {"definition": "lever", "targets": ["GATE_A", "GATE_FEHLT"], "action": "open"}}},
  {"id": 4, "type": "trigger", "name": "PLATE_LEER", "pos": [6, 0, 0],
   "components": {"trigger": {"filter": "any", "plate": {}}}},
  {"id": 5, "type": "relay", "name": "RELAY_1", "components": {"relay": {"targets": ["RELAY_2"]}}},
  {"id": 6, "type": "relay", "name": "RELAY_2", "components": {"relay": {"targets": ["RELAY_1"]}}},
  {"id": 7, "type": "trigger", "name": "TELE_WEG", "pos": [9, 0, 0],
   "components": {"trigger": {"teleport": {"to": "NIRGENDS"}}}},
  {"id": 8, "type": "trigger", "name": "TELE_EBENE", "pos": [9, 0, 3],
   "components": {"trigger": {"teleport": {"world": "dungeon_2.g7world", "start": "START_FEHLT"}}}},
  {"id": 9, "type": "spawn", "name": "SPAWN_X", "components": {"spawn": {"npc": "mon_unbekannt"}}},
  {"id": 10, "type": "mesh", "pos": [0, 0, 12], "mesh": "testworld/block.gltf",
   "components": {"mover": {"kind": "secret_wall"}}},
  {"id": 11, "type": "relay", "name": "RELAY_LEER"},
  {"id": 12, "type": "mesh", "name": "GATE_A", "pos": [5, 0, 12], "mesh": "testworld/block.gltf"},
  {"id": 13, "type": "trigger", "name": "TELE_OK", "pos": [9, 0, 6],
   "components": {"trigger": {"teleport": {"to": "START_OBEN"}}}}
]})");
    REQUIRE(file.ok());
    world::Scene scene;
    REQUIRE(world::spawnWorld(scene, file.value()).ok());
    scene.updateTransforms();

    const std::vector<DungeonLink> links = dungeonLinks(scene);
    const auto link = [&](u64 from, std::string_view target)
    {
        return std::ranges::find_if(links, [&](const DungeonLink& l)
                                    { return l.from.value == from && l.target == target; });
    };
    REQUIRE(link(3, "GATE_A") != links.end());
    CHECK(link(3, "GATE_A")->to.value == 2); // the first of the two of that name
    CHECK(link(3, "GATE_A")->toPosition == Vec3(0, 0, 6));
    CHECK(linkColor(*link(3, "GATE_A")).g > 0.9f); // open: green
    CHECK_FALSE(link(3, "GATE_FEHLT")->to.valid());
    REQUIRE(link(13, "START_OBEN") != links.end());
    CHECK(link(13, "START_OBEN")->kind == DungeonLink::Kind::Teleport);

    const StartsFn starts = [](std::string_view w) -> std::optional<std::vector<std::string>>
    {
        if (w == "dungeon_2.g7world")
        {
            return std::vector<std::string>{"START_E02"};
        }
        return std::nullopt;
    };
    const std::vector<std::string> npcs{"mon_gatterbock"};
    const std::vector<TriggerIssue> issues = checkDungeon(scene, starts, &npcs);
    const auto about = [&](u64 id)
    {
        std::vector<std::string> texts;
        for (const TriggerIssue& i : issues)
        {
            if (i.id.value == id)
            {
                texts.push_back(i.text);
            }
        }
        return texts;
    };
    CHECK(mentions(about(3), "'GATE_FEHLT': no vob"));
    CHECK(mentions(about(3), "'GATE_A': 2 vobs"));
    CHECK(mentions(about(4), "plate without targets"));
    CHECK(mentions(about(5), "cycle"));
    CHECK(mentions(about(6), "cycle"));
    CHECK(mentions(about(7), "no vob 'NIRGENDS'"));
    CHECK(mentions(about(8), "no start point 'START_FEHLT'"));
    CHECK(mentions(about(9), "mon_unbekannt"));
    CHECK(mentions(about(10), "without a name"));
    CHECK(mentions(about(11), "relay without targets"));
    CHECK(about(13).empty());
    CHECK(about(2).empty());
    CHECK(std::ranges::is_sorted(issues, {}, [](const TriggerIssue& i) { return i.id.value; }));
    // Without scripts the NPC instances are not checked.
    CHECK(std::ranges::none_of(checkDungeon(scene, starts, nullptr),
                               [](const TriggerIssue& i) { return i.id.value == 9; }));
}

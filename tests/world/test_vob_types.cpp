// Vob types start, sound, trigger and mob (M4): .g7world round trip, errors, triggers, start points.

#include <g7/world/Scene.hpp>
#include <g7/world/StartPoints.hpp>
#include <g7/world/Triggers.hpp>
#include <g7/world/WorldFile.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::world;

namespace
{
const char* const kWorld = R"({"version": 1, "name": "types", "vobs": [
  {"id": 1, "type": "start", "name": "START_MARKTPLATZ", "pos": [0, 0, 8]},
  {"id": 2, "type": "start", "name": "START_UEBERSICHT", "pos": [0, 40, 60], "rot": [-0.258819, 0, 0, 0.965926]},
  {"id": 3, "type": "sound", "name": "SND_FIRE", "pos": [1, 0, 2],
   "components": {"sound": {"sound": "FIRE_CRACKLE", "range": 12, "volume": 0.8, "mode": "random", "delay": [2, 6]}}},
  {"id": 4, "type": "trigger", "name": "TRG_GATE", "pos": [10, 0, 0], "rot": [0, 0.382683, 0, 0.92388],
   "components": {"trigger": {"shape": "box", "halfExtents": [2, 3, 0.5], "onEnter": "GATE_ENTER",
                              "onLeave": "GATE_LEAVE", "target": "DOOR_01"}}},
  {"id": 5, "type": "trigger", "name": "TRG_WELL", "pos": [-5, 0, 0],
   "components": {"trigger": {"shape": "sphere", "radius": 2, "filter": "any", "once": true, "target": 7}}},
  {"id": 6, "type": "mob", "name": "BED_01", "pos": [3, 0, 3], "mesh": "models/bed.glb",
   "components": {"mob": {"definition": "BEDHIGH"}}}
]})";

WorldFile parse(std::string_view text)
{
    auto world = parseWorldFile(text, "types.g7world");
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

TEST_CASE("Vob types: start, sound, trigger and mob are read, spawned, captured and written unchanged")
{
    const WorldFile file = parse(kWorld);
    REQUIRE(file.vobs.size() == 6);
    CHECK(file.vobs[0].type == VobType::Start);
    const SoundEmitter& sound = file.vobs[2].sound;
    CHECK(sound.sound == "FIRE_CRACKLE");
    CHECK(sound.range == 12.0f);
    CHECK(sound.volume == doctest::Approx(0.8f));
    CHECK(sound.mode == SoundEmitter::Mode::Random);
    CHECK(sound.delay == Vec2(2.0f, 6.0f));
    const TriggerVolume& gate = file.vobs[3].trigger;
    CHECK(gate.shape == TriggerVolume::Shape::Box);
    CHECK(gate.halfExtents == Vec3(2.0f, 3.0f, 0.5f));
    CHECK(gate.onEnter == "GATE_ENTER");
    CHECK(gate.filter == TriggerVolume::Filter::Player); // default
    CHECK_FALSE(gate.once);
    CHECK(gate.targetName == "DOOR_01");
    const TriggerVolume& well = file.vobs[4].trigger;
    CHECK(well.shape == TriggerVolume::Shape::Sphere);
    CHECK(well.radius == 2.0f);
    CHECK(well.filter == TriggerVolume::Filter::Any);
    CHECK(well.once);
    CHECK(well.targetId == VobId{7});
    CHECK(file.vobs[5].mesh == "models/bed.glb");
    CHECK(file.vobs[5].mob.definition == "BEDHIGH");

    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    const entt::entity bed = scene.findById(VobId{6});
    CHECK(scene.get<MeshRef>(bed)->path == "models/bed.glb"); // drawn like a mesh
    CHECK(scene.get<MobRef>(bed)->definition == "BEDHIGH");
    CHECK(scene.has<StartPoint>(scene.findById(VobId{1})));
    CHECK(scene.get<SoundEmitter>(scene.findById(VobId{3}))->sound == "FIRE_CRACKLE");

    // File -> scene -> file -> text is stable.
    const std::string written = writeWorldFile(captureWorld(scene, "types"));
    CHECK(writeWorldFile(parse(written)) == written);
    CHECK(written.find(R"("components":{"mob":{"definition":"BEDHIGH"}})") != std::string::npos);
    CHECK(written.find(R"("target":"DOOR_01")") != std::string::npos);
    CHECK(written.find(R"("target":7)") != std::string::npos);
    CHECK(
        written.find(
            R"({"id":1,"type":"start","name":"START_MARKTPLATZ","pos":[0.0,0.0,8.0],"rot":[0.0,0.0,0.0,1.0]})") !=
        std::string::npos);
}

TEST_CASE("Vob types: broken components are errors naming the entry")
{
    CHECK(errorOf(R"({"id": 1, "type": "sound"})") == "w.g7world: vobs[0].components.sound: needs 'sound'");
    CHECK(
        errorOf(R"({"id": 1, "type": "sound", "components": {"sound": {"sound": "A", "mode": "often"}}})") ==
        "w.g7world: vobs[0].components.sound: 'mode' must be one of 'loop', 'random'");
    CHECK(errorOf(R"({"id": 1, "type": "sound", "components": {"sound": {"sound": "A", "volume": 2}}})") ==
          "w.g7world: vobs[0].components.sound: 'volume' must lie in 0..1");
    CHECK(
        errorOf(R"({"id": 1, "type": "sound", "components": {"sound": {"sound": "A", "delay": [5, 1]}}})") ==
        "w.g7world: vobs[0].components.sound: 'delay' must be [min, max] seconds with 0 <= min <= max");
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"shape": "cone"}}})") ==
          "w.g7world: vobs[0].components.trigger: 'shape' must be 'box' or 'sphere'");
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"halfExtents": [1, 0, 1]}}})") ==
          "w.g7world: vobs[0].components.trigger: 'halfExtents' must be positive");
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"filter": "monster"}}})") ==
          "w.g7world: vobs[0].components.trigger: 'filter' must be one of 'player', 'npc', 'any'");
    CHECK(errorOf(R"({"id": 1, "type": "trigger", "components": {"trigger": {"target": 0}}})") ==
          "w.g7world: vobs[0].components.trigger: 'target' must be a vob id or a vob name");
    CHECK(errorOf(R"({"id": 1, "type": "mob", "components": {"mob": {"definition": "BED"}}})") ==
          "w.g7world: vobs[0]: a mob vob needs 'mesh' (VFS path)");
    CHECK(errorOf(R"({"id": 1, "type": "mob", "mesh": "bed.glb"})") ==
          "w.g7world: vobs[0].components.mob: needs 'definition'");
    CHECK(errorOf(R"({"id": 1, "type": "spawn"})") == "w.g7world: vobs[0]: unknown type 'spawn'");
    CHECK(errorOf(R"({"id": 1, "type": "start"})") ==
          "w.g7world: vobs[0]: a start vob needs a 'name' (--start selects it)");
    CHECK(
        errorOf(
            R"({"id": 1, "type": "start", "name": "START_A"}, {"id": 2, "type": "start", "name": "start_a"})") ==
        "w.g7world: vobs[1]: start point name 'start_a' is already used by vobs[0]");
}

TEST_CASE("Triggers: enter and leave in a fixed order, turned boxes, spheres, filters, once")
{
    Scene scene;
    REQUIRE(spawnWorld(scene, parse(kWorld)).ok());
    scene.updateTransforms();
    TriggerSystem triggers;
    std::vector<std::string> log;
    triggers.setCallback(
        [&](const TriggerEvent& e)
        {
            log.push_back(std::string(e.kind == TriggerEvent::Kind::Enter ? "enter " : "leave ") +
                          std::to_string(e.trigger.value) + " " + std::to_string(e.who.value) + " " +
                          std::string(e.function));
        });

    // The gate box (half 2 x 3 x 0.5) is turned 45 degrees about Y: (11.2, 0, -1.2) lies inside along
    // its local x axis, (11.2, 0, 1.2) is 1.7 m off its thin side.
    const entt::entity gate = scene.findById(VobId{4});
    const Mat4& gateMatrix = scene.get<WorldTransform>(gate)->matrix;
    CHECK(TriggerSystem::contains(*scene.get<TriggerVolume>(gate), gateMatrix, Vec3(11.2f, 0.0f, -1.2f)));
    CHECK_FALSE(
        TriggerSystem::contains(*scene.get<TriggerVolume>(gate), gateMatrix, Vec3(11.2f, 0.0f, 1.2f)));

    // Player 100 in the gate, NPC 201 in the gate (ignored: player filter), NPC 200 and player 50 at the
    // well (filter any). Events come sorted by trigger, then probe - not in probe order.
    const std::vector<TriggerProbe> probes{{VobId{200}, Vec3(-5.5f, 0.0f, 0.0f), false},
                                           {VobId{100}, Vec3(11.2f, 0.0f, -1.2f), true},
                                           {VobId{201}, Vec3(10.0f, 0.0f, 0.0f), false},
                                           {VobId{50}, Vec3(-4.0f, 0.0f, 0.5f), true}};
    const auto events = triggers.update(scene, probes);
    CHECK(events.size() == 3);
    CHECK(log == std::vector<std::string>{"enter 4 100 GATE_ENTER", "enter 5 50 ", "enter 5 200 "});
    CHECK(triggers.isInside(VobId{4}, VobId{100}));
    CHECK_FALSE(triggers.isInside(VobId{4}, VobId{201}));

    // Nothing changes: no events. Then everybody leaves (the probes are gone).
    log.clear();
    triggers.update(scene, probes);
    CHECK(log.empty());
    triggers.update(scene, {});
    CHECK(log == std::vector<std::string>{"leave 4 100 GATE_LEAVE", "leave 5 50 ", "leave 5 200 "});

    // The well is "once": spent after its first enter; the gate fires again.
    log.clear();
    triggers.update(scene, probes);
    CHECK(log == std::vector<std::string>{"enter 4 100 GATE_ENTER"});
    triggers.reset();
    log.clear();
    triggers.update(scene, probes);
    CHECK(log.size() == 3); // reset forgets the spent once-trigger
}

TEST_CASE("Start points: lowest id by default, by name, errors list the known ones")
{
    Scene scene;
    REQUIRE(spawnWorld(scene, parse(kWorld)).ok());
    auto first = findStartPoint(scene);
    REQUIRE(first.ok());
    CHECK(scene.idOf(first.value()) == VobId{1});
    auto overview = findStartPoint(scene, "start_uebersicht");
    REQUIRE(overview.ok());
    CHECK(scene.idOf(overview.value()) == VobId{2});
    auto unknown = findStartPoint(scene, "START_KIRCHE");
    REQUIRE_FALSE(unknown.ok());
    CHECK(unknown.error().message ==
          "unknown start point 'START_KIRCHE' (the world has: START_MARKTPLATZ, START_UEBERSICHT)");
    Scene empty;
    CHECK(findStartPoint(empty).error().message == "the world has no start point");
}

TEST_CASE("Vob category: mesh vobs default to deco, gameplay is written, mobs are always gameplay")
{
    const WorldFile file = parse(R"({"version": 1, "vobs": [
      {"id": 1, "type": "mesh", "name": "TREE", "mesh": "tree.glb"},
      {"id": 2, "type": "mesh", "name": "SIGNPOST", "mesh": "sign.glb", "category": "gameplay"},
      {"id": 3, "type": "mob", "name": "BED", "mesh": "bed.glb", "components": {"mob": {"definition": "BED"}}}]})");
    CHECK(file.vobs[0].category == VobCategory::Deco);
    CHECK(file.vobs[1].category == VobCategory::Gameplay);
    CHECK(file.vobs[2].category == VobCategory::Gameplay);
    Scene scene;
    REQUIRE(spawnWorld(scene, file).ok());
    CHECK(scene.get<MeshRef>(scene.findById(VobId{1}))->category == VobCategory::Deco);
    CHECK(scene.get<MeshRef>(scene.findById(VobId{2}))->category == VobCategory::Gameplay);
    CHECK(scene.get<MeshRef>(scene.findById(VobId{3}))->category == VobCategory::Gameplay);

    // Only gameplay is written: deco worlds stay byte for byte as they were.
    const std::string written = writeWorldFile(captureWorld(scene, "w"));
    CHECK(written.find(R"("mesh":"tree.glb"})") != std::string::npos);
    CHECK(written.find(R"("mesh":"sign.glb","category":"gameplay"})") != std::string::npos);
    CHECK(written.find(R"("mesh":"bed.glb","components")") != std::string::npos); // mobs: implied
    CHECK(writeWorldFile(parse(written)) == written);

    CHECK(errorOf(R"({"id": 1, "type": "mesh", "mesh": "a.glb", "category": "scenery"})") ==
          "w.g7world: vobs[0]: 'category' must be 'deco' or 'gameplay'");
    CHECK(errorOf(R"({"id": 1, "type": "mob", "mesh": "a.glb", "category": "deco",
                      "components": {"mob": {"definition": "BED"}}})") ==
          "w.g7world: vobs[0]: a mob vob is always 'gameplay'");
}

// Lights and mobs in the editor (M16 part D): light presets, the preview hour, the budget hint of E1; mob
// definitions from lists, what a definition holds, the checks against NPCs, guilds and items; a door stored
// open as world.md says (written only when true); undo of light and mob edits through the history.

#include <g7/editor/Commands.hpp>
#include <g7/editor/LightEdit.hpp>
#include <g7/editor/MobEdit.hpp>
#include <g7/editor/Operations.hpp>
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
const char* const kWorld = R"({"version": 1, "nextVobId": 20, "vobs": [
  {"id": 1, "type": "mob", "name": "CHEST", "pos": [0, 0, 0], "mesh": "mobs/chest.glb", "components": {"mob": {"definition": "mob_camp_chest"}}},
  {"id": 2, "type": "mob", "name": "DOOR", "pos": [5, 0, 0], "mesh": "mobs/door.glb", "components": {"mob": {"definition": "door", "open": true}}},
  {"id": 3, "type": "mob", "name": "BENCH_OPEN", "pos": [10, 0, 0], "mesh": "mobs/bench.glb", "components": {"mob": {"definition": "bench", "open": true}}},
  {"id": 4, "type": "mob", "name": "GONE", "pos": [15, 0, 0], "mesh": "mobs/chest.glb", "components": {"mob": {"definition": "mob_nowhere"}}},
  {"id": 5, "type": "mob", "name": "BAD_CHEST", "pos": [20, 0, 0], "mesh": "mobs/chest.glb", "components": {"mob": {"definition": "mob_bad_chest"}}},
  {"id": 6, "type": "light", "name": "LAMP", "pos": [0, 2, 0], "components": {"light": {"range": 4}}}
]})";

MobLists lists()
{
    MobLists l;
    MobDefinitionInfo chest;
    chest.definition = "mob_camp_chest";
    chest.name = "Truhe";
    chest.type = "chest";
    chest.owner = "npc_farmer_woman";
    chest.lock = "LRRL";
    chest.key = "it_key_camp";
    chest.contents = {{"it_apple", 3}, {"it_bread", 1}};
    MobDefinitionInfo bad;
    bad.definition = "mob_bad_chest";
    bad.type = "crate"; // not in mobs.toml
    bad.owner = "npc_nobody";
    bad.lock = "LXR";
    bad.key = "it_nothing";
    bad.contents = {{"it_ghost", 1}};
    l.definitions = {chest, bad};
    l.types = {"bench", "chest", "door"};
    l.npcs = {"npc_farmer_woman", "npc_gate_guard"};
    l.guilds = {"farmer", "guard"};
    l.items = {"it_apple", "it_bread", "it_key_camp"};
    return l;
}

/// The vobs as the file writes them (ids handed out stay used after an undo, so nextVobId is left out).
std::string vobsText(const world::Scene& scene)
{
    world::WorldFile file = world::captureWorld(scene, "w");
    file.nextVobId = 0;
    return world::writeWorldFile(file);
}

void load(world::Scene& scene)
{
    auto world = world::parseWorldFile(kWorld, "w.g7world");
    REQUIRE(world.ok());
    REQUIRE(world::spawnWorld(scene, world.value()).ok());
    scene.updateTransforms();
}
} // namespace

TEST_CASE("Light edit: presets, the preview hour, the budget hint")
{
    REQUIRE(lightPresets().size() == 5);
    const LightPreset* window = findLightPreset("window");
    REQUIRE(window != nullptr);
    CHECK(window->light.daylight);
    CHECK_FALSE(findLightPreset("fire")->light.daylight);
    CHECK(findLightPreset("laser") == nullptr);
    CHECK(splitHour(13.5f).hour == 13);
    CHECK(splitHour(13.5f).minute == 30);
    CHECK(splitHour(24.0f).hour == 23);
    CHECK(splitHour(24.0f).minute == 59);
    CHECK(splitHour(-3.0f).hour == 0);
    CHECK(lightBudgetText(12, 128, 340, 20) ==
          "12 light(s) this frame (max 128), 340 in the world, 20 window light(s)");
    CHECK(lightBudgetText(128, 128, 400, 0).find("budget full") != std::string::npos);
}

TEST_CASE("Mob edit: definitions and types as one list; a bare type; the summary")
{
    const MobLists l = lists();
    CHECK(mobDefinitionChoices(l.definitions, l.types) ==
          std::vector<std::string>{"bench", "chest", "door", "mob_bad_chest", "mob_camp_chest"});
    const auto chest = resolveMob(l.definitions, l.types, "mob_camp_chest");
    REQUIRE(chest.has_value());
    CHECK(chest->owner == "npc_farmer_woman");
    const auto bare = resolveMob(l.definitions, l.types, "bench");
    REQUIRE(bare.has_value());
    CHECK(bare->type == "bench");
    CHECK(bare->lock.empty());
    CHECK_FALSE(resolveMob(l.definitions, l.types, "mob_nowhere").has_value());
    const auto summary = mobSummary(*chest);
    const auto line = [&](std::string_view label)
    {
        const auto it = std::ranges::find(summary, label, &std::pair<std::string, std::string>::first);
        return it != summary.end() ? it->second : std::string("?");
    };
    CHECK(line("mob type") == "chest");
    CHECK(line("mob lock") == "LRRL");
    CHECK(line("mob contents") == "it_apple x3, it_bread");
    CHECK(mobSummary(*bare)[2].second == "-"); // no owner
}

TEST_CASE("Mob edit: checks against mobs.toml, NPCs, guilds and items; only doors are stored open")
{
    world::Scene scene;
    load(scene);
    const std::vector<MobIssue> issues = checkMobs(scene, lists());
    const auto about = [&](u64 id)
    {
        std::vector<std::string> texts;
        for (const MobIssue& i : issues)
        {
            if (i.id.value == id)
            {
                texts.push_back(i.text);
            }
        }
        return texts;
    };
    CHECK(about(1).empty());
    CHECK(about(2).empty()); // a door may be open
    REQUIRE(about(3).size() == 1);
    CHECK(about(3)[0].find("only doors") != std::string::npos);
    REQUIRE(about(4).size() == 1);
    CHECK(about(4)[0].find("neither") != std::string::npos);
    const std::vector<std::string> bad = about(5);
    CHECK(bad.size() == 5); // type, owner, key, contents, lock
    CHECK(std::ranges::any_of(bad,
                              [](const std::string& t) { return t.find("mobs.toml") != std::string::npos; }));
    CHECK(std::ranges::any_of(bad, [](const std::string& t)
                              { return t.find("npc_nobody") != std::string::npos; }));
    CHECK(std::ranges::any_of(bad,
                              [](const std::string& t) { return t.find("it_ghost") != std::string::npos; }));
    CHECK(std::ranges::any_of(bad, [](const std::string& t) { return t.find("LXR") != std::string::npos; }));
    // A guild as owner is fine.
    MobLists guild = lists();
    guild.definitions[0].owner = "guard";
    CHECK(checkMobs(scene, guild).size() == issues.size());

    // world.md: "open" written only when true.
    const world::WorldFile file = world::captureWorld(scene, "w");
    const auto door =
        std::ranges::find(file.vobs, 2u, [](const world::WorldFileVob& v) { return v.id.value; });
    const auto chest =
        std::ranges::find(file.vobs, 1u, [](const world::WorldFileVob& v) { return v.id.value; });
    CHECK(world::writeVobJson(*door).find("\"open\":true") != std::string::npos);
    CHECK(world::writeVobJson(*chest).find("open") == std::string::npos);
}

TEST_CASE("Light and mob edit: undo and redo of a new light and of a mob's values")
{
    world::Scene scene;
    load(scene);
    const std::string original = vobsText(scene);
    History history;

    // A new light (no state before: it was not there).
    const world::VobId next{scene.nextVobId()};
    Command place{"place LIGHT_FIRE_01", captureVobs(scene, std::vector{next}), {}, false};
    REQUIRE(placeLight(scene, "LIGHT_FIRE_01", Vec3(1, 2, 3), findLightPreset("fire")->light).ok());
    place.after = captureVobs(scene, std::vector{next});
    REQUIRE(place.after[0].json.has_value());
    CHECK(place.after[0].json->find("\"flicker\"") != std::string::npos);
    history.record(std::move(place));

    // The door closed, the lamp a window light.
    const std::vector<world::VobId> ids{world::VobId{2}, world::VobId{6}};
    Command change{"door and lamp", captureVobs(scene, ids), {}, false};
    scene.get<world::MobRef>(scene.findById(world::VobId{2}))->open = false;
    scene.get<world::LightSource>(scene.findById(world::VobId{6}))->daylight = true;
    change.after = captureVobs(scene, ids);
    history.record(std::move(change));
    const std::string edited = vobsText(scene);

    REQUIRE(history.undo(scene).value());
    REQUIRE(history.undo(scene).value());
    CHECK(vobsText(scene) == original);
    REQUIRE(history.redo(scene).value());
    REQUIRE(history.redo(scene).value());
    CHECK(vobsText(scene) == edited);
}

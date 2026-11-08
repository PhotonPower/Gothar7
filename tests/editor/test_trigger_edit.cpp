// Trigger editing (M16 part C): the inspector's choice lists (script functions, worlds, start points) and the
// checks of a world's triggers - unknown functions, level changes to missing worlds or start points.

#include <g7/editor/TriggerEdit.hpp>
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

TEST_CASE("Trigger edit: choice lists keep unknown names visible; '-' is none")
{
    const std::vector<std::string> known{"camp_enter", "camp_leave"};
    const ChoiceList none = choiceList("", known);
    CHECK(none.items == std::vector<std::string>{"-", "camp_enter", "camp_leave"});
    CHECK(none.index == 0);
    CHECK(choiceValue(none.items, none.index).empty());
    const ChoiceList found = choiceList("camp_leave", known);
    CHECK(found.index == 2);
    CHECK(choiceValue(found.items, found.index) == "camp_leave");
    const ChoiceList unknown = choiceList("gone_function", known);
    CHECK(unknown.items.back() == "gone_function");
    CHECK(choiceValue(unknown.items, unknown.index) == "gone_function");
    CHECK(choiceValue(unknown.items, 99).empty());
}

TEST_CASE("Trigger edit: start names; checks of functions and level changes")
{
    auto world = world::parseWorldFile(R"({"version": 1, "nextVobId": 20, "vobs": [
  {"id": 1, "type": "start", "name": "START_LAGER", "pos": [0, 0, 0]},
  {"id": 2, "type": "trigger", "name": "TRG_OK", "pos": [0, 1, 0],
   "components": {"trigger": {"onEnter": "camp_enter"}}},
  {"id": 3, "type": "trigger", "name": "TRG_GONE", "pos": [5, 1, 0],
   "components": {"trigger": {"onEnter": "gone_function", "onLeave": "camp_leave"}}},
  {"id": 4, "type": "trigger", "name": "TRG_TO_CITY", "pos": [10, 1, 0],
   "components": {"trigger": {"changeWorld": {"world": "worlds/city.g7world", "start": "start_tor"}}}},
  {"id": 5, "type": "trigger", "name": "TRG_TO_NOWHERE", "pos": [15, 1, 0],
   "components": {"trigger": {"changeWorld": {"world": "worlds/nowhere.g7world", "start": "X"}}}},
  {"id": 6, "type": "trigger", "name": "TRG_BAD_START", "pos": [20, 1, 0],
   "components": {"trigger": {"changeWorld": {"world": "worlds/city.g7world", "start": "START_MARKT"}}}},
  {"id": 7, "type": "trigger", "name": "TRG_IDLE", "pos": [25, 1, 0], "components": {"trigger": {}}}
]})",
                                       "w.g7world");
    REQUIRE(world.ok());
    CHECK(startNames(world.value()) == std::vector<std::string>{"START_LAGER"});
    world::Scene scene;
    REQUIRE(world::spawnWorld(scene, world.value()).ok());

    const std::vector<std::string> functions{"camp_enter", "camp_leave"};
    const StartsFn starts = [](std::string_view w) -> std::optional<std::vector<std::string>>
    {
        if (w == "worlds/city.g7world")
        {
            return std::vector<std::string>{"START_TOR"};
        }
        return std::nullopt;
    };
    const std::vector<TriggerIssue> issues = checkTriggers(scene, &functions, starts);
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
    CHECK(about(2).empty());
    REQUIRE(about(3).size() == 1);
    CHECK(about(3)[0].find("gone_function") != std::string::npos);
    CHECK(about(4).empty()); // start names ignore case, as the engine
    REQUIRE(about(5).size() == 1);
    CHECK(about(5)[0].find("no world") != std::string::npos);
    REQUIRE(about(6).size() == 1);
    CHECK(about(6)[0].find("START_MARKT") != std::string::npos);
    REQUIRE(about(7).size() == 1);
    CHECK(about(7)[0].find("does nothing") != std::string::npos);
    CHECK(std::ranges::is_sorted(issues, {}, [](const TriggerIssue& i) { return i.id.value; }));
    CHECK(issues[0].name == "TRG_GONE");

    // Without scripts the functions are not checked.
    CHECK(std::ranges::none_of(checkTriggers(scene, nullptr, starts), [](const TriggerIssue& i)
                               { return i.text.find("script function") != std::string::npos; }));
}

// Routine preview (M16 part E): where a routine puts an NPC at an hour (the camp's farmer woman), the day
// plan checks (gaps, overlaps, malformed times, places the waynet lacks).

#include <g7/editor/RoutinePreview.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::editor;

namespace
{
/// routines/camp.lua, rtn_farmer_woman.
RoutineInfo farmerWoman()
{
    return {"rtn_farmer_woman",
            "routines/camp.lua",
            {{"06:00", "12:00", "zs_sweep", "wp_camp_center"},
             {"12:00", "19:00", "zs_campfire", "wp_camp_fire"},
             {"19:00", "23:00", "zs_sit_campfire", "wp_camp_fire"},
             {"23:00", "06:00", "zs_sleep", "wp_camp_west"}}};
}

bool has(const std::vector<RoutineIssue>& issues, std::string_view text)
{
    return std::ranges::any_of(issues,
                               [&](const RoutineIssue& i) { return i.text.find(text) != std::string::npos; });
}
} // namespace

TEST_CASE("Routine preview: times; the entry at each hour, across midnight")
{
    CHECK(parseMinute("08:30") == 510u);
    CHECK(parseMinute("24:00") == 1440u);
    CHECK_FALSE(parseMinute("24:30").has_value());
    CHECK_FALSE(parseMinute("8:30").has_value());
    CHECK(minuteText(510) == "08:30");
    const RoutineInfo r = farmerWoman();
    const auto stateAt = [&](u32 hour)
    {
        const auto i = entryAt(r, hour * 60);
        return i ? r.entries[*i].state + "@" + r.entries[*i].at : std::string("-");
    };
    CHECK(stateAt(8) == "zs_sweep@wp_camp_center");
    CHECK(stateAt(12) == "zs_campfire@wp_camp_fire"); // [from, to): 12:00 belongs to the next entry
    CHECK(stateAt(20) == "zs_sit_campfire@wp_camp_fire");
    CHECK(stateAt(23) == "zs_sleep@wp_camp_west");
    CHECK(stateAt(3) == "zs_sleep@wp_camp_west"); // across midnight
    CHECK(entryAt(r, 1440 + 8 * 60) == entryAt(r, 8 * 60));
}

TEST_CASE("Routine preview: a full day without issues; gaps, overlaps, bad times, unknown places")
{
    const PlaceKnownFn known = [](std::string_view at) { return at.starts_with("wp_camp_"); };
    CHECK(checkRoutine(farmerWoman(), known).empty());

    RoutineInfo r{"rtn_test",
                  "routines/camp_editor.lua",
                  {{"08:00", "12:00", "zs_stand", "wp_camp_center"},
                   {"11:00", "18:00", "zs_sweep", "wp_nowhere"},
                   {"8:00", "9:00", "zs_stand", "wp_camp_fire"},
                   {"20:00", "20:00", "zs_stand", "wp_camp_fire"},
                   {"18:00", "20:00", "", ""}}};
    const std::vector<RoutineIssue> issues = checkRoutine(r, known);
    CHECK(has(issues, "nothing to do 20:00-24:00"));
    CHECK(has(issues, "nothing to do 00:00-08:00"));
    CHECK(has(issues, "entries overlap 11:00-12:00"));
    CHECK(has(issues, "the waynet has no 'wp_nowhere'"));
    CHECK(has(issues, "is not HH:MM"));
    CHECK(has(issues, "is empty"));
    CHECK(has(issues, "no state"));
    CHECK(has(issues, "no place"));
    CHECK(std::ranges::all_of(issues, [](const RoutineIssue& i) { return i.routine == "rtn_test"; }));
    CHECK(has(checkRoutine({"rtn_empty", "", {}}, known), "no entries"));
}

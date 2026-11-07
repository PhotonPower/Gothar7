// Routines without a text editor (M16 part E, decision 4): the editor's own Lua file per site - written in a
// fixed order and read back to the same routines (round trip); lines the editor does not write are refused.

#include <g7/editor/RoutineEdit.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::editor;

TEST_CASE("Routine edit: the site's file name; routine names")
{
    CHECK(siteOf("testworld/camp.g7world") == "camp");
    CHECK(siteOf("worlds/leonberg/Leonberg.g7world") == "leonberg");
    CHECK(editorRoutineFile("camp") == "routines/camp_editor.lua");
    CHECK(validRoutineName("rtn_baker_2"));
    CHECK_FALSE(validRoutineName("rtn_"));
    CHECK_FALSE(validRoutineName("baker"));
    CHECK_FALSE(validRoutineName("rtn_Baker"));
    CHECK_FALSE(validRoutineName("rtn_a\"b"));
}

TEST_CASE("Routine edit: written in a fixed order, read back the same")
{
    const std::vector<RoutineInfo> routines{
        {"rtn_smith",
         "routines/camp_editor.lua",
         {{"20:00", "06:00", "zs_sleep", "wp_camp_west"}, {"06:00", "20:00", "zs_smith", "FP_WORK_CAMP_01"}}},
        {"rtn_baker", "routines/camp_editor.lua", {{"05:00", "05:30", "zs_stand", "wp_camp_center"}}},
        {"rtn_empty", "routines/camp_editor.lua", {}},
    };
    const std::string text = writeRoutines(routines, "camp");
    CHECK(text.starts_with("-- Written by the Gothar editor"));
    CHECK(text.find("Routine \"rtn_baker\"") < text.find("Routine \"rtn_empty\"")); // by name
    CHECK(text.find("from = \"06:00\"") < text.find("from = \"20:00\""));           // entries by start
    CHECK(text.find(
              "    { from = \"05:00\", to = \"05:30\", state = \"zs_stand\", at = \"wp_camp_center\" },\n") !=
          std::string::npos);
    CHECK(writeRoutines(routines, "camp") == text); // same routines, same bytes

    auto back = parseRoutines(text, "routines/camp_editor.lua");
    REQUIRE(back.ok());
    REQUIRE(back.value().size() == 3);
    CHECK(back.value()[0].name == "rtn_baker");
    CHECK(back.value()[2].name == "rtn_smith");
    CHECK(back.value()[2].entries[0].from == "06:00"); // sorted when written
    CHECK(back.value()[2].file == "routines/camp_editor.lua");
    CHECK(writeRoutines(back.value(), "camp") == text);

    // CRLF line ends read the same.
    std::string crlf;
    for (const char c : text)
    {
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    }
    REQUIRE(parseRoutines(crlf, "f").ok());
    CHECK(parseRoutines(crlf, "f").value().size() == 3);
}

TEST_CASE("Routine edit: lines the editor does not write are refused")
{
    CHECK_FALSE(parseRoutines("Routine \"rtn_a\" {\n    print('x')\n}\n", "f").ok());
    CHECK_FALSE(parseRoutines("Routine \"rtn_a\" {\n", "f").ok()); // not closed
    CHECK_FALSE(parseRoutines("function evil() end\n", "f").ok());
    const auto error = parseRoutines("-- ok\n\nRoutine \"rtn_a\" {\n}\nx = 1\n", "routines/x_editor.lua");
    REQUIRE_FALSE(error.ok());
    CHECK(error.error().message.find("routines/x_editor.lua:5") != std::string::npos);
    CHECK(parseRoutines("", "f").value().empty());
}

TEST_CASE("Routine edit: entries sorted by their start, malformed ones last")
{
    RoutineInfo r{
        "rtn_x", "", {{"bad", "x", "s", "a"}, {"12:00", "13:00", "s", "a"}, {"06:00", "12:00", "s", "a"}}};
    sortEntries(r);
    CHECK(r.entries[0].from == "06:00");
    CHECK(r.entries[1].from == "12:00");
    CHECK(r.entries[2].from == "bad");
}

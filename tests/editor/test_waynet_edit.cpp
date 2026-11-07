// Waynet editing (M16 part B): adding, moving, renaming, joining and deleting points as undoable snapshots;
// the contract's ownership rules (moved or renamed generated points become the human's); names; welt's
// annotation line; and the checks of world.md's recommendations.

#include <g7/editor/Commands.hpp>
#include <g7/editor/WaynetCheck.hpp>
#include <g7/editor/WaynetEdit.hpp>
#include <g7/world/Scene.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::editor;

namespace
{
/// Three points in a row 10 m apart (A-B generated, B-C by hand) and a freepoint near A.
world::WaynetData net()
{
    world::WaynetData w;
    w.points = {{"WP_CAMP_01", Vec3(0, 0, 0), std::nullopt, true},
                {"WP_CAMP_02", Vec3(10, 0, 0), std::nullopt, true},
                {"WP_CAMP_03", Vec3(20, 0, 0), std::nullopt, false}};
    w.edges = {{"WP_CAMP_01", "WP_CAMP_02", true}, {"WP_CAMP_02", "WP_CAMP_03", false}};
    w.freepoints = {{"FP_SIT_CAMP_01", Vec3(1, 0, 1), Vec3(0, 0, -1), true}};
    REQUIRE(world::normalizeWaynet(w, "test").ok());
    return w;
}

bool joined(const world::WaynetData& w, std::string_view a, std::string_view b)
{
    return std::ranges::any_of(w.edges, [&](const world::WaynetEdge& e)
                               { return edgeKey(e.a, e.b) == edgeKey(a, b); });
}
} // namespace

TEST_CASE("Waynet edit: names - suggested free, stems, welt's annotation line")
{
    const world::WaynetData w = net();
    CHECK(suggestName(w, "WP_", "CAMP") == "WP_CAMP_04");
    CHECK(suggestName(w, "FP_SIT_", "CAMP") == "FP_SIT_CAMP_02");
    CHECK(suggestName(w, "WP_", "NEU") == "WP_NEU_01");
    CHECK(nameStem("WP_LEO_MARKT_07") == "LEO_MARKT");
    CHECK(nameStem("FP_SIT_CAMP_01") == "CAMP");
    CHECK(nameStem("WP_CAMP_GATE") == "CAMP_GATE");
    CHECK(removalAnnotation("WP_LEO_MARKT_07") == "\"WP_LEO_MARKT_07\",");
}

TEST_CASE("Waynet edit: add, join, move, rename, delete; generated points become the human's when moved")
{
    world::WaynetData w = net();
    REQUIRE(addPoint(w, "WP_CAMP_04", Vec3(30, 0, 0)).ok());
    CHECK_FALSE(addPoint(w, "WP_CAMP_04", Vec3(31, 0, 0)).ok()); // taken
    CHECK_FALSE(addPoint(w, "wp_bad", Vec3(31, 0, 0)).ok());     // invalid name
    REQUIRE(connect(w, "WP_CAMP_03", "WP_CAMP_04").value());
    CHECK_FALSE(connect(w, "WP_CAMP_04", "WP_CAMP_03").value());  // already
    CHECK_FALSE(connect(w, "WP_CAMP_04", "FP_SIT_CAMP_01").ok()); // freepoints have no edges
    CHECK(joined(w, "WP_CAMP_03", "WP_CAMP_04"));

    CHECK(isGeneratedEntry(w, "WP_CAMP_02"));
    REQUIRE(moveEntry(w, "WP_CAMP_02", Vec3(10, 0, 2)).ok());
    CHECK_FALSE(isGeneratedEntry(w, "WP_CAMP_02")); // contract: moved, it is the human's
    REQUIRE(moveEntry(w, "FP_SIT_CAMP_01", Vec3(1, 0, 1), Vec3(1, 0, 0)).ok());
    CHECK(w.findFreepoint("FP_SIT_CAMP_01")->dir->x == doctest::Approx(1.0f));

    REQUIRE(renameEntry(w, "WP_CAMP_01", "WP_CAMP_TOR").ok());
    CHECK(w.findPoint("WP_CAMP_01") == nullptr);
    CHECK(joined(w, "WP_CAMP_TOR", "WP_CAMP_02")); // the edge followed
    CHECK_FALSE(renameEntry(w, "WP_CAMP_TOR", "WP_CAMP_03").ok());

    REQUIRE(removeEntry(w, "WP_CAMP_03").ok());
    CHECK_FALSE(joined(w, "WP_CAMP_02", "WP_CAMP_03")); // its edges went with it
    CHECK_FALSE(joined(w, "WP_CAMP_03", "WP_CAMP_04"));
    CHECK(disconnect(w, "WP_CAMP_TOR", "WP_CAMP_02").value());
    CHECK_FALSE(disconnect(w, "WP_CAMP_TOR", "WP_CAMP_02").value());
}

TEST_CASE("Waynet edit: snapshots undo and redo an edit exactly, with the edges at the points")
{
    world::WaynetData w = net();
    const world::WaynetData start = w;
    // Delete the middle point (both its edges go), then undo, then redo.
    const std::vector<std::string> names{"WP_CAMP_02"};
    const WaynetSnapshot before = captureWaynet(w, names);
    CHECK(before.edges.size() == 2);
    REQUIRE(removeEntry(w, "WP_CAMP_02").ok());
    const WaynetSnapshot after = captureWaynet(w, names, before.edges);
    const world::WaynetData deleted = w;
    REQUIRE(restoreWaynet(w, before).ok());
    CHECK(w == start);
    REQUIRE(restoreWaynet(w, after).ok());
    CHECK(w == deleted);

    // A rename touches the old and the new name.
    w = start;
    const std::vector<std::string> both{"WP_CAMP_03", "WP_CAMP_ENDE"};
    const WaynetSnapshot renameBefore = captureWaynet(w, both);
    REQUIRE(renameEntry(w, "WP_CAMP_03", "WP_CAMP_ENDE").ok());
    const WaynetSnapshot renameAfter = captureWaynet(w, both, renameBefore.edges);
    const world::WaynetData renamed = w;
    REQUIRE(restoreWaynet(w, renameBefore).ok());
    CHECK(w == start);
    REQUIRE(restoreWaynet(w, renameAfter).ok());
    CHECK(w == renamed);

    // Through the history of commands (as the editor records them).
    w = start;
    History history;
    Command add{"add WP_CAMP_04", {}, {}, false};
    const std::vector<std::string> fresh{"WP_CAMP_04"};
    const std::vector<std::pair<std::string, std::string>> edge{edgeKey("WP_CAMP_03", "WP_CAMP_04")};
    add.waynetBefore = captureWaynet(w, fresh, edge);
    REQUIRE(addPoint(w, "WP_CAMP_04", Vec3(30, 0, 0)).ok());
    REQUIRE(connect(w, "WP_CAMP_03", "WP_CAMP_04").ok());
    add.waynetAfter = captureWaynet(w, fresh, edge);
    const world::WaynetData added = w;
    history.record(std::move(add));
    world::Scene scene; // no vobs: only the waynet changes
    REQUIRE(history.undo(scene, &w).value());
    CHECK(w == start);
    REQUIRE(history.redo(scene, &w).value());
    CHECK(w == added);
}

TEST_CASE("Waynet check: unconnected, islands, edge lengths, walls, ground, lone freepoints")
{
    world::WaynetData w;
    w.points = {{"WP_A", Vec3(0, 0, 0)},   {"WP_B", Vec3(10, 0, 0)},  {"WP_C", Vec3(12, 0, 0)},
                {"WP_D", Vec3(100, 0, 0)}, {"WP_E", Vec3(130, 0, 0)}, {"WP_F", Vec3(200, 0, 0)}};
    w.edges = {{"WP_A", "WP_B"}, {"WP_B", "WP_C"}, {"WP_D", "WP_E"}};
    w.freepoints = {{"FP_SIT_X", Vec3(1, 0, 2), Vec3(0, 0, 1)}, {"FP_SIT_Y", Vec3(50, 0, 0), Vec3(0, 0, 1)}};
    REQUIRE(world::normalizeWaynet(w, "test").ok());
    // A wall across x = 5 (between A and B); no ground under E.
    const BlockedFn wall = [](const Vec3& a, const Vec3& b)
    { return std::min(a.x, b.x) < 5.0f && std::max(a.x, b.x) > 5.0f; };
    const GroundFn ground = [](const Vec3& p) { return p.x != 130.0f; };
    const std::vector<WaynetIssue> issues = checkWaynet(w, {}, wall, ground);
    const auto has = [&](WaynetIssue::Kind kind, std::string_view name)
    {
        return std::ranges::any_of(issues,
                                   [&](const WaynetIssue& i) { return i.kind == kind && i.name == name; });
    };
    using K = WaynetIssue::Kind;
    CHECK(has(K::Unconnected, "WP_F"));
    CHECK(has(K::Island, "WP_D")); // the main net is A-B-C (3 points)
    CHECK(has(K::Island, "WP_E"));
    CHECK_FALSE(has(K::Island, "WP_A"));
    CHECK(has(K::ShortEdge, "WP_B")); // B-C 2 m
    CHECK(has(K::LongEdge, "WP_D"));  // D-E 30 m
    CHECK(has(K::Blocked, "WP_A"));   // A-B through the wall
    CHECK_FALSE(has(K::Blocked, "WP_B"));
    CHECK(has(K::NotOnGround, "WP_E"));
    CHECK(has(K::LoneFreepoint, "FP_SIT_Y"));
    CHECK_FALSE(has(K::LoneFreepoint, "FP_SIT_X")); // A is 2.2 m away
    // Sorted by kind, every issue has a text.
    CHECK(std::ranges::is_sorted(issues, {}, [](const WaynetIssue& i) { return i.kind; }));
    CHECK(std::ranges::none_of(issues, [](const WaynetIssue& i) { return i.text.empty(); }));
    // Without callbacks the world checks are left out.
    CHECK(std::ranges::none_of(checkWaynet(w, {}, {}, {}), [](const WaynetIssue& i)
                               { return i.kind == K::Blocked || i.kind == K::NotOnGround; }));
}

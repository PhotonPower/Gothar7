// Autopilot routes (gothar --walk): route.json version 1 (docs/modules/tools.md "Autopilot").

#include <g7/walk/Route.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;
using namespace g7::walk;

TEST_CASE("Route: all fields, defaults and point names")
{
    auto route = parseRoute(R"({
      "version": 1, "start": "START_MARKTPLATZ", "gait": "walk", "time": "11:00", "timeLimit": 120,
      "screenshotEveryM": 25,
      "points": [
        {"name": "P01", "pos": [12.5, -40], "gait": "run", "screenshot": true, "radius": 2.5},
        {"pos": [30, -55], "action": "climb"},
        {"name": "SCHLOSS", "pos": [41, -60], "teleport": true, "y": 12.0, "action": "jump"}
      ]})",
                            "r.json");
    REQUIRE_MESSAGE(route.ok(), (route.ok() ? "" : route.error().message));
    const Route& r = route.value();
    CHECK(r.start == "START_MARKTPLATZ");
    CHECK(r.gait == Gait::Walk);
    CHECK(r.time == "11:00");
    CHECK(r.timeLimit == doctest::Approx(120.0));
    CHECK(r.screenshotEveryM == doctest::Approx(25.0f));
    REQUIRE(r.points.size() == 3);
    CHECK(r.points[0].pos == Vec2(12.5f, -40.0f));
    CHECK(r.points[0].gait == Gait::Run);
    CHECK(r.points[0].radius == doctest::Approx(2.5f));
    CHECK(r.points[0].screenshot);
    CHECK(r.points[1].name == "P002"); // unnamed: numbered
    CHECK(r.points[1].radius == doctest::Approx(1.0f));
    CHECK(r.points[1].action == RoutePoint::Action::Climb);
    CHECK_FALSE(r.points[1].gait.has_value());
    CHECK(r.points[2].teleport);
    CHECK(r.points[2].y == doctest::Approx(12.0f));
    CHECK(r.points[2].action == RoutePoint::Action::Jump);

    auto all = parseRoute(R"({"version": 1, "screenshot": "all", "points": [{"pos": [0, 0]}]})", "a.json");
    REQUIRE(all.ok());
    CHECK(all.value().points[0].screenshot);
    CHECK(all.value().gait == Gait::Run);
    CHECK(all.value().timeLimit == doctest::Approx(900.0));
    CHECK(gaitName(Gait::Sneak) == "sneak");
}

TEST_CASE("Route: errors name the file and the point")
{
    const auto fails = [](const char* json, const char* expected)
    {
        auto r = parseRoute(json, "bad.json");
        REQUIRE_FALSE(r.ok());
        CHECK_MESSAGE(r.error().message.find(expected) != std::string::npos, r.error().message);
        CHECK(r.error().message.starts_with("bad.json"));
    };
    fails("{nope", "not a JSON object");
    fails(R"({"points": [{"pos": [0, 0]}]})", "\"version\": 1");
    fails(R"({"version": 1})", "points");
    fails(R"({"version": 1, "points": []})", "points");
    fails(R"({"version": 1, "points": [{"pos": [0]}]})", "points[0]: needs 'pos'");
    fails(R"({"version": 1, "points": [{"pos": [0, 0]}, {"pos": [1, 1], "action": "fly"}]})",
          "points[1]: 'action'");
    fails(R"({"version": 1, "points": [{"pos": [0, 0], "radius": 0}]})", "'radius' must be positive");
    fails(R"({"version": 1, "gait": "crawl", "points": [{"pos": [0, 0]}]})", "'gait'");
    fails(R"({"version": 1, "screenshot": true, "points": [{"pos": [0, 0]}]})", "\"all\"");
    fails(R"({"version": 1, "timeLimit": -5, "points": [{"pos": [0, 0]}]})", "'timeLimit'");
}

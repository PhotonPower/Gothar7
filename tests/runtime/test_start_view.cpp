// Reproducible views: the start options --cam/--yaw/--pitch/--fly/--player and the line copy_position (F6)
// copies - parsing, formatting, and that the line parses back to the same view.

#include <g7/runtime/StartView.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <sstream>
#include <string>

using namespace g7;

TEST_CASE("Start view: numbers and x,y,z")
{
    CHECK(parseNumber("-10.5").value() == doctest::Approx(-10.5f));
    CHECK(parseNumber("45").value() == doctest::Approx(45.0f));
    CHECK_FALSE(parseNumber("").ok());
    CHECK_FALSE(parseNumber("4x").ok());
    CHECK_FALSE(parseNumber("nan").ok());
    const Vec3 v = parseVec3("103.2,4.1,-55").value();
    CHECK(v.x == doctest::Approx(103.2f));
    CHECK(v.y == doctest::Approx(4.1f));
    CHECK(v.z == doctest::Approx(-55.0f));
    CHECK_FALSE(parseVec3("1,2").ok());
    CHECK_FALSE(parseVec3("1,2,3,4").ok());
    CHECK_FALSE(parseVec3("1,,3").ok());
    CHECK_FALSE(parseVec3("1, 2, 3").ok()); // no spaces: it is one command-line argument
    CHECK(parseVec3("1,2").error().message.find("'1,2'") != std::string::npos);
}

TEST_CASE("Start view: command-line options")
{
    StartView view;
    CHECK(view.empty());
    CHECK(parseStartViewArgument("--cam=1,2,3", view).value());
    CHECK(parseStartViewArgument("--yaw=45", view).value());
    CHECK(parseStartViewArgument("--pitch=-10.5", view).value());
    CHECK(parseStartViewArgument("--fly", view).value());
    CHECK(parseStartViewArgument("--player=4,5,6", view).value());
    CHECK_FALSE(parseStartViewArgument("--world=x.g7world", view).value()); // not a view option
    CHECK_FALSE(parseStartViewArgument("--flying", view).value());
    CHECK_FALSE(parseStartViewArgument("--cam=1,2", view).ok());
    CHECK_FALSE(parseStartViewArgument("--yaw=left", view).ok());
    REQUIRE(view.camera.has_value());
    CHECK(view.camera->z == doctest::Approx(3.0f));
    CHECK(*view.yawDegrees == doctest::Approx(45.0f));
    CHECK(*view.pitchDegrees == doctest::Approx(-10.5f));
    CHECK(view.fly);
    CHECK(view.player->x == doctest::Approx(4.0f));
    CHECK_FALSE(view.empty());
}

TEST_CASE("Start view: the copied line, and back")
{
    ViewLine line;
    line.world = "worlds/leonberg/leonberg.g7world";
    line.position = Vec3(103.204f, 4.1f, -55.0f);
    line.yawDegrees = 405.0f; // wraps to 45
    line.pitchDegrees = -10.04f;
    line.minuteOfDay = 12 * 60 + 5;
    CHECK(formatViewLine(line) ==
          "--world=worlds/leonberg/leonberg.g7world --cam=103.20,4.10,-55.00 --yaw=45.0 "
          "--pitch=-10.0 --time=12:05 --fly");
    line.fly = false;
    line.yawDegrees = -0.01f; // no "-0.0"
    CHECK(formatViewLine(line) ==
          "--world=worlds/leonberg/leonberg.g7world --player=103.20,4.10,-55.00 --yaw=0.0 --time=12:05");
    line.world.clear(); // no world: no --world
    line.fly = true;
    line.yawDegrees = 180.0f;
    CHECK(formatViewLine(line).starts_with("--cam=103.20,4.10,-55.00 --yaw=180.0"));
    line.world = "C:/Gothar Daten/w.g7world"; // spaces: quoted for the shell
    CHECK(formatViewLine(line).starts_with("--world=\"C:/Gothar Daten/w.g7world\" "));

    // Every option of a line parses back to the view it describes.
    line.world = "w.g7world";
    std::istringstream words(formatViewLine(line));
    StartView view;
    std::string word;
    while (words >> word)
    {
        const auto taken = parseStartViewArgument(word, view);
        REQUIRE(taken.ok());
        CHECK(taken.value() == !(word.starts_with("--world=") || word.starts_with("--time=")));
    }
    CHECK(view.fly);
    CHECK(view.camera->x == doctest::Approx(103.2f));
    CHECK(*view.yawDegrees == doctest::Approx(180.0f));
    CHECK(*view.pitchDegrees == doctest::Approx(-10.0f));
}

TEST_CASE("Fly mode hint: from the bindings, unbound parts left out")
{
    using platform::Action;
    platform::ActionMap actions;
    const auto bind = [&](Action action, const char* input)
    { actions.bind(action, *platform::bindingFromName(input)); };
    // Nothing bound (a config without [bindings]): no dashes, only what needs no key.
    CHECK(flyHintText(actions, 10.0f) == "fly: mouse look, wheel speed (10 m/s)");

    bind(Action::FlyForward, "W");
    bind(Action::FlyLeft, "A");
    bind(Action::FlyBack, "S");
    bind(Action::FlyRight, "D");
    bind(Action::FlyUp, "Space");
    bind(Action::FlyUp, "E"); // only the first input of an action is shown
    bind(Action::FlyDown, "LeftCtrl");
    bind(Action::FlyFast, "LeftShift");
    bind(Action::CopyPosition, "F6");
    bind(Action::DebugFly, "F3");
    CHECK(flyHintText(actions, 12.4f) ==
          "fly: WASD move, Space/LeftCtrl up/down, LeftShift fast, mouse look, wheel "
          "speed (12 m/s)\nF6 copy position, F3 back");

    platform::ActionMap arrows;
    for (const auto& [action, input] :
         {std::pair{Action::FlyForward, "Up"}, std::pair{Action::FlyLeft, "Left"},
          std::pair{Action::FlyBack, "Down"}, std::pair{Action::FlyRight, "Right"}})
    {
        arrows.bind(action, *platform::bindingFromName(input));
    }
    const std::string text = flyHintText(arrows, 10.0f);
    CHECK(text == "fly: Up/Left/Down/Right move, mouse look, wheel speed (10 m/s)");
    CHECK(text.find('-') == std::string::npos);
}

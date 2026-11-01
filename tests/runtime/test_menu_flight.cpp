// The camera flight behind the main menu (M14 part F): parsing, steady speed, smooth through the points,
// loops.
#include <g7/runtime/MenuFlight.hpp>

#include <doctest/doctest.h>

#include <ostream>

using namespace g7;

TEST_CASE("Menu flight: points from data, through every point at a steady speed, looping")
{
    auto config = Config::parse(R"(
version = 1
speed = 2.0
[[point]]
position = [0, 10, 0]
look = [0, 0, -10]
[[point]]
position = [20, 10, 0]
look = [20, 0, -10]
[[point]]
position = [20, 10, 20]
look = [20, 0, 10]
)");
    REQUIRE(config.ok());
    auto flight = MenuFlight::fromConfig(config.value());
    REQUIRE(flight.ok());
    const MenuFlight& f = flight.value();
    // 20 m + 20 m + back 28.3 m at 2 m/s.
    CHECK(f.duration() == doctest::Approx(10.0f + 10.0f + 14.142f).epsilon(0.001));
    CHECK(f.at(0.0f).position.x == doctest::Approx(0.0f));
    CHECK(f.at(10.0f).position.x == doctest::Approx(20.0f));
    CHECK(f.at(10.0f).look.z == doctest::Approx(-10.0f));
    CHECK(f.at(20.0f).position.z == doctest::Approx(20.0f));
    CHECK(f.at(f.duration() + 10.0f).position.x == doctest::Approx(20.0f)); // loops
    const MenuFlightPose mid = f.at(5.0f);
    CHECK(mid.position.x > 5.0f);
    CHECK(mid.position.x < 15.0f);
    CHECK(mid.position.y == doctest::Approx(10.0f));

    CHECK_FALSE(
        MenuFlight::fromConfig(Config::parse("version = 1\n[[point]]\nposition = [0, 0, 0]\nlook = [1, 0, "
                                             "0]\n")
                                   .value())
            .ok()); // one point
    CHECK_FALSE(MenuFlight::fromConfig(Config::parse("version = 2").value()).ok());
}

TEST_CASE("Menu flight: the shipped flight file is valid")
{
    const fs::Path path = fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "ui" / "menu_flight.toml";
    auto config = Config::load(path);
    REQUIRE(config.ok());
    auto flight = MenuFlight::fromConfig(config.value());
    REQUIRE(flight.ok());
    CHECK(flight.value().duration() > 60.0f); // slow (F8)
}

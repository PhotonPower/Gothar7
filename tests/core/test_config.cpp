#include <g7/core/Config.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <ostream> // doctest needs it to print std::string_view operands
#include <string>
#include <vector>

using namespace g7;

namespace
{
constexpr std::string_view kSample = R"(
# Engine defaults
title = "Gothar"

[render]
vsync = true
fps_limit = 144
view_distance = 250.5
resolution_scale = 1

[audio.volume]
music = 0.8
effects = 1.0

[bindings]
move_forward = ["W", "Up"]
action = ["LeftCtrl"]
draw_weapon = ["Space"]
)";

Config sample()
{
    auto parsed = Config::parse(kSample, "sample.toml");
    REQUIRE(parsed.ok());
    return std::move(parsed).value();
}
} // namespace

TEST_CASE("Config: reads all value types via dotted keys")
{
    const Config config = sample();
    CHECK(config.get<std::string>("title", "") == "Gothar");
    CHECK(config.get<bool>("render.vsync", false));
    CHECK(config.get<i64>("render.fps_limit", 0) == 144);
    CHECK(config.get<f64>("render.view_distance", 0.0) == doctest::Approx(250.5));
    CHECK(config.get<f64>("audio.volume.music", 0.0) == doctest::Approx(0.8));
    CHECK(config.get<std::vector<std::string>>("bindings.move_forward", {}) ==
          std::vector<std::string>{"W", "Up"});
}

TEST_CASE("Config: defaults for missing keys and wrong types")
{
    const Config config = sample();
    CHECK(config.get<i64>("render.missing", 42) == 42);
    CHECK(config.get<i64>("nope.deeper.key", 7) == 7);
    CHECK_FALSE(config.contains("render.missing"));
    CHECK(config.contains("render.vsync"));
    CHECK(config.contains("audio.volume"));

    // Wrong types fall back to the default / nullopt.
    CHECK(config.get<i64>("title", -1) == -1);
    CHECK_FALSE(config.find<bool>("render.fps_limit").has_value());
    CHECK_FALSE(config.find<std::string>("render").has_value());
    // Integers are accepted where a float is requested, not the other way round.
    CHECK(config.find<f64>("render.resolution_scale") == 1.0);
    CHECK_FALSE(config.find<i64>("render.view_distance").has_value());

    CHECK(config.find<i64>("render.fps_limit") == 144);
}

TEST_CASE("Config: mixed arrays are not string lists")
{
    auto parsed = Config::parse("list = [\"a\", 1]");
    REQUIRE(parsed.ok());
    CHECK_FALSE(parsed.value().find<std::vector<std::string>>("list").has_value());
}

TEST_CASE("Config: parse errors report source and line")
{
    auto parsed = Config::parse("a = 1\nb = = 2\n", "broken.toml");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error().message.find("broken.toml:2:") == 0);
}

TEST_CASE("Config: set creates tables and replaces values")
{
    Config config;
    config.set<i64>("render.shadows.cascades", 4);
    config.set<bool>("render.vsync", false);
    config.set<std::string>("player.name", "Held");
    config.set<std::vector<std::string>>("bindings.jump", {"Alt", "Gamepad_A"});
    CHECK(config.get<i64>("render.shadows.cascades", 0) == 4);
    CHECK_FALSE(config.get<bool>("render.vsync", true));
    CHECK(config.get<std::string>("player.name", "") == "Held");
    CHECK(config.get<std::vector<std::string>>("bindings.jump", {}).size() == 2);

    config.set<f64>("render.vsync", 0.5); // type change is allowed
    CHECK(config.get<f64>("render.vsync", 0.0) == doctest::Approx(0.5));

    config.set<i64>("player.name.length", 4); // value turned into a table
    CHECK(config.get<i64>("player.name.length", 0) == 4);
}

TEST_CASE("Config: keys of a table")
{
    const Config config = sample();
    CHECK(config.keys("bindings") == std::vector<std::string>{"action", "draw_weapon", "move_forward"});
    CHECK(config.keys() == std::vector<std::string>{"audio", "bindings", "render", "title"});
    CHECK(config.keys("title").empty());
    CHECK(config.keys("missing").empty());
}

TEST_CASE("Config: merge applies overrides recursively")
{
    Config config = sample();
    auto user = Config::parse(R"(
[render]
fps_limit = 60
[audio.volume]
music = 0.2
[bindings]
action = ["E"]
[extra]
debug = true
)");
    REQUIRE(user.ok());
    config.merge(user.value());

    CHECK(config.get<i64>("render.fps_limit", 0) == 60);
    CHECK(config.get<bool>("render.vsync", false)); // neighbour kept
    CHECK(config.get<f64>("audio.volume.music", 0.0) == doctest::Approx(0.2));
    CHECK(config.get<f64>("audio.volume.effects", 0.0) == doctest::Approx(1.0));
    CHECK(config.get<std::vector<std::string>>("bindings.action", {}) == std::vector<std::string>{"E"});
    CHECK(config.get<std::vector<std::string>>("bindings.move_forward", {}).size() == 2);
    CHECK(config.get<bool>("extra.debug", false));
}

TEST_CASE("Config: copies are independent")
{
    Config a = sample();
    Config b = a;
    b.set<i64>("render.fps_limit", 30);
    CHECK(a.get<i64>("render.fps_limit", 0) == 144);
    CHECK(b.get<i64>("render.fps_limit", 0) == 30);
}

TEST_CASE("Config: save and load round trip")
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path dir = std::filesystem::temp_directory_path() / ("g7_config_test_" + std::to_string(stamp));
    REQUIRE(fs::createDirectories(dir).ok());
    const fs::Path file = dir / "engine.toml";

    const Config original = sample();
    REQUIRE(original.save(file).ok());
    auto loaded = Config::load(file);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().toToml() == original.toToml());
    CHECK(loaded.value().get<f64>("audio.volume.music", 0.0) == doctest::Approx(0.8));

    auto missing = Config::load(dir / "missing.toml");
    CHECK_FALSE(missing.ok());

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

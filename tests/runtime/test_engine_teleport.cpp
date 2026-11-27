// Teleporters and falls in the engine (docs/modules/dungeon.md D4) on testworld/teleport.g7world: a
// teleporter puts the hero at a marker with its yaw and does not send him back from the teleporter he lands
// in; an inactive one waits for an event; items go through one that takes them; another teleporter leads to
// another level; a fall through a shaft arrives below unhurt; switched teleporters survive a save game.

#include "RunTempDir.hpp"

#include <g7/runtime/Engine.hpp>
#include <g7/save/GameState.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig teleportConfig()
{
    EngineConfig config;
    config.appName = "teleport";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/teleport.g7world");
    config.start = "START_TELEPORT";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    REQUIRE_MESSAGE(result.ok(), std::string(line), ": ", (result.ok() ? "" : result.error().message));
    return result.value();
}

void frames(Engine& engine, int n)
{
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

Vec3 heroFeet(Engine& engine)
{
    const physics::CharacterController* player = engine.player();
    REQUIRE(player != nullptr);
    return player->feet();
}
} // namespace

TEST_CASE("Engine teleport: to a marker in the same world, no bouncing back, off until switched on")
{
    Engine engine(teleportConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    frames(engine, 3);
    CHECK(engine.teleportActive("TELE_A") == std::optional<bool>(true));
    CHECK(engine.teleportActive("TELE_AUS") == std::optional<bool>(false));
    CHECK_FALSE(engine.teleportActive("NOTHING").has_value());
    run(engine, "Story.went = '' on('hero_teleported', function(t) Story.went = t end)");

    // Into TELE_A: at MARK_B (inside TELE_B, which leads back - he stays).
    run(engine, "teleport(5, 0, 0)");
    frames(engine, 3);
    Vec3 feet = heroFeet(engine);
    CHECK(feet.x == doctest::Approx(20.0f).epsilon(0.01));
    CHECK(feet.z == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(run(engine, "Story.went").asString() == "TELE_A");
    CHECK(engine.screenFade() > 0.0f); // the white flash
    frames(engine, 30);
    CHECK(engine.screenFade() == 0.0f);
    feet = heroFeet(engine);
    CHECK(feet.x == doctest::Approx(20.0f).epsilon(0.01)); // not sent back
    // Out and in again: TELE_B takes him back to the start, turned by its yaw.
    run(engine, "teleport(23, 0, 0)");
    frames(engine, 3);
    run(engine, "teleport(20, 0, 0)");
    frames(engine, 3);
    feet = heroFeet(engine);
    CHECK(feet.x == doctest::Approx(0.0f).epsilon(0.01));

    // The inactive one does nothing until an event switches it on.
    run(engine, "teleport(-5, 0, 0)");
    frames(engine, 3);
    CHECK(heroFeet(engine).x == doctest::Approx(-5.0f).epsilon(0.01));
    run(engine, "teleport(-8, 0, 0) send('TELE_AUS', 'on')");
    frames(engine, 3);
    CHECK(run(engine, "teleport_state('TELE_AUS')").asBool());
    run(engine, "teleport(-5, 0, 0)");
    frames(engine, 3);
    CHECK(heroFeet(engine).x == doctest::Approx(20.0f).epsilon(0.01));
}

TEST_CASE("Engine teleport: items go through a teleporter that takes them")
{
    Engine engine(teleportConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    const auto apple = engine.spawnItem("it_apple", 1, Vec3(5.0f, 0.0f, 0.3f));
    REQUIRE(apple.ok());
    frames(engine, 2);
    bool moved = false;
    for (const WorldItemInfo& item : engine.worldItems())
    {
        if (item.vob == apple.value())
        {
            moved = std::abs(item.position.x - 20.0f) < 0.1f;
        }
    }
    CHECK(moved);
}

TEST_CASE("Engine teleport: to another level, and a fall through a shaft arrives below unhurt")
{
    Engine engine(teleportConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "teleport(0, 0, -10)");
    frames(engine, 3);
    CHECK(engine.worldPath() == "testworld/teleport_unten.g7world");
    CHECK(heroFeet(engine).x == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(engine.screenFade() > 0.0f); // black, fading

    // Back up (a fresh engine is simpler) and down the shaft: he arrives 6 m up and lands without harm.
    Engine shaft(teleportConfig());
    REQUIRE(shaft.init().ok());
    frames(shaft, 3);
    const i64 hp = run(shaft, "stat('hp')").asInteger();
    run(shaft, "teleport(0, 0, 10)");
    frames(shaft, 3);
    CHECK(shaft.worldPath() == "testworld/teleport_unten.g7world");
    frames(shaft, 120);
    REQUIRE(shaft.lastLanding().has_value());
    CHECK(shaft.lastLanding()->height > 4.0f); // would hurt otherwise (from 4 m)
    CHECK(shaft.lastLanding()->damage == 0.0f);
    CHECK(run(shaft, "stat('hp')").asInteger() == hp);
}

TEST_CASE("Engine teleport: switched teleporters are saved and come back")
{
    std::string saved;
    {
        Engine engine(teleportConfig());
        REQUIRE(engine.init().ok());
        frames(engine, 3);
        run(engine, "send('TELE_AUS', 'on') send('TELE_A', 'off')");
        frames(engine, 2);
        auto state = engine.captureGame();
        REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
        REQUIRE(state.value().worlds.back().teleporters.size() == 2);
        saved = save::toJson(state.value());
    }
    Engine engine(teleportConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    auto state = save::fromJson(saved);
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    REQUIRE(engine.applyGame(state.value()).ok());
    CHECK(engine.teleportActive("TELE_AUS") == std::optional<bool>(true));
    CHECK(engine.teleportActive("TELE_A") == std::optional<bool>(false));
}

TEST_CASE("Engine teleport: a level change into the own world is a move to the start, nothing reset or saved")
{
    EngineConfig config = teleportConfig();
    const std::filesystem::path saves = test::runTempDir() / "teleport_same_world";
    config.saveDirectory = saves;
    Engine engine(config);
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    // State of this world: a switched teleporter and an item put down.
    run(engine, "send('TELE_AUS', 'on')");
    const auto apple = engine.spawnItem("it_apple", 1, Vec3(3.0f, 0.0f, 3.0f));
    REQUIRE(apple.ok());
    run(engine, "teleport(8, 0, 8)");
    frames(engine, 3);

    engine.requestWorldChange("TestWorld/Teleport.g7world", "START_TELEPORT"); // the path in other case
    frames(engine, 3);
    CHECK(engine.worldPath() == "testworld/teleport.g7world");
    CHECK(heroFeet(engine).x == doctest::Approx(0.0f).epsilon(0.01)); // at the start point
    CHECK(heroFeet(engine).z == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(engine.teleportActive("TELE_AUS") == std::optional<bool>(true)); // not read fresh from the file
    bool appleThere = false;
    for (const WorldItemInfo& item : engine.worldItems())
    {
        appleThere = appleThere || item.vob == apple.value();
    }
    CHECK(appleThere);
    CHECK(engine.keptWorlds() == 0);
    frames(engine, 240); // an autosave would come within a few seconds
    CHECK_FALSE(std::filesystem::exists(saves / "auto.g7save"));

    // An unknown start point in the own world: a warning, he stays.
    run(engine, "teleport(8, 0, 8)");
    frames(engine, 2);
    engine.requestWorldChange("testworld/teleport.g7world", "NO_SUCH_START");
    frames(engine, 2);
    CHECK(heroFeet(engine).x == doctest::Approx(8.0f).epsilon(0.01));
}

// The dungeon's definition of done (docs/modules/dungeon.md D8, "Tests"): on the engine's two small test
// levels testworld/dungeon_1 and dungeon_2 the mechanics work together - a lever opens a gate; an item put
// down on a plate holds a gate open and taken away shuts it; two plates on a relay; a hidden button opens a
// secret wall with a niche and its treasure; a trapdoor drops the hero into level 2 still falling; an
// invisible teleporter brings him back; a resting monster wakes as he comes near; a save in the middle of it
// brings everything back.

#include <g7/runtime/Engine.hpp>
#include <g7/save/GameState.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
constexpr const char* kLevel1 = "testworld/dungeon_1.g7world";
constexpr const char* kLevel2 = "testworld/dungeon_2.g7world";

EngineConfig dungeonConfig()
{
    EngineConfig config;
    config.appName = "dungeon";
    config.headless = true;
    config.world = fs::fromUtf8(kLevel1);
    config.start = "START_E01";
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

/// Uses a mob as with the action key and waits until the hero is done.
void use(Engine& engine, std::string_view mob)
{
    run(engine, std::format("use_mob('{}')", mob));
    frames(engine, 1);
    for (int i = 0; i < 600 && engine.mobPhase().has_value(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    REQUIRE_FALSE(engine.mobPhase().has_value());
    frames(engine, 40); // the event, the mover's way
}

bool open(const Engine& engine, std::string_view mover)
{
    const auto info = engine.moverInfo(mover);
    REQUIRE(info.has_value());
    return info->open;
}

Vec3 heroFeet(Engine& engine)
{
    const physics::CharacterController* player = engine.player();
    REQUIRE(player != nullptr);
    return player->feet();
}

bool dormant(Engine& engine, std::string_view npc)
{
    return run(engine, std::format("npc_dormant('{}')", npc)).asBool();
}

/// Picks up the item in focus (the hero faces it) and waits for the clip.
void pickUp(Engine& engine)
{
    engine.updateFocus();
    REQUIRE(engine.focus().has_value());
    REQUIRE(engine.focus()->kind == gameplay::FocusKind::Item);
    REQUIRE(engine.pickUpFocus().ok());
    for (int i = 0; i < 300 && engine.pickingUp(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    frames(engine, 40);
}
} // namespace

TEST_CASE("Dungeon scenario: a lever opens a gate; an item on a plate holds a gate open, taken away shuts it")
{
    Engine engine(dungeonConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    frames(engine, 3);
    CHECK(engine.worldFile().environment.has_value()); // a level without sky (D7b)

    CHECK_FALSE(open(engine, "GATE_E01_TOR"));
    use(engine, "LEVER_E01_TOR");
    CHECK(open(engine, "GATE_E01_TOR"));
    CHECK(engine.moverInfo("GATE_E01_TOR")->position == 1.0f);

    CHECK_FALSE(open(engine, "GATE_E01_PLATTE"));
    REQUIRE(engine.spawnItem("it_apple", 1, Vec3(9.0f, 0.0f, -6.0f)).ok());
    frames(engine, 40);
    CHECK(engine.platePressed("PLATE_E01_STEIN"));
    CHECK(open(engine, "GATE_E01_PLATTE"));
    run(engine, "teleport(9, 0, -4.6)"); // facing -Z: the apple in front
    frames(engine, 2);
    pickUp(engine);
    CHECK_FALSE(engine.platePressed("PLATE_E01_STEIN"));
    CHECK_FALSE(open(engine, "GATE_E01_PLATTE"));
}

TEST_CASE("Dungeon scenario: two plates on a relay - the hero on one and an apple on the other")
{
    Engine engine(dungeonConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "Story.heard = {} on('dungeon_event', function(target, action, source) "
                "Story.heard[#Story.heard + 1] = target .. ' ' .. action end)");
    run(engine, "teleport(-14, 0, -6)"); // the player-only plate
    frames(engine, 5);
    CHECK(engine.platePressed("PLATE_E01_LINKS"));
    CHECK(run(engine, "#Story.heard").asInteger() == 0); // the relay waits for both
    REQUIRE(engine.spawnItem("it_apple", 1, Vec3(-14.0f, 0.0f, -10.0f)).ok());
    frames(engine, 10);
    CHECK(engine.platePressed("PLATE_E01_RECHTS"));
    REQUIRE(run(engine, "#Story.heard").asInteger() == 1);
    CHECK(run(engine, "Story.heard[1]").asString() == "LUA_E01_BEIDE on");
}

TEST_CASE("Dungeon scenario: a hidden button opens a secret wall; behind it a niche holds the treasure")
{
    Engine engine(dungeonConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "teleport(-4, 0, 0.5)");
    frames(engine, 2);
    engine.updateFocus();
    REQUIRE(engine.focus().has_value());
    CHECK(engine.focus()->name == engine.uiText("UI_LOOSE_STONE"));
    CHECK_FALSE(open(engine, "WALL_E01_NISCHE"));
    use(engine, "BUTTON_E01_STEIN");
    CHECK(open(engine, "WALL_E01_NISCHE"));

    // The niche behind it: the hero reaches in and takes what is there.
    run(engine, "teleport(-9, 0, 9)");
    frames(engine, 2);
    run(engine, "use_mob('NISCHE_E01')");
    for (int i = 0; i < 300 && engine.mobPhase() != std::optional<std::string_view>("loop"); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.inventoryOpen());
    const auto niche = engine.findMob("NISCHE_E01");
    REQUIRE(niche.has_value());
    const i64 gulden = run(engine, "item_count('it_gulden')").asInteger();
    REQUIRE(engine.takeFromMob(*niche, "it_ring_protection").ok());
    REQUIRE(engine.takeFromMob(*niche, "it_gulden", 25).ok());
    CHECK(run(engine, "item_count('it_ring_protection')").asInteger() == 1);
    CHECK(run(engine, "item_count('it_gulden')").asInteger() == gulden + 25);
}

TEST_CASE(
    "Dungeon scenario: a trapdoor drops the hero into level 2 still falling; an invisible teleporter brings "
    "him back")
{
    Engine engine(dungeonConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    const i64 hp = run(engine, "stat('hp')").asInteger();
    run(engine, "teleport(0, 0, 12.3)"); // onto the trapdoor's leaf: its trigger opens it, he falls
    bool arrived = false;
    f32 arrivalSpeed = 0.0f;
    for (int i = 0; i < 300 && !arrived; ++i)
    {
        REQUIRE(engine.runFrame());
        if (engine.worldPath() == kLevel2 && engine.player() != nullptr)
        {
            arrived = true;
            arrivalSpeed = -engine.player()->velocity().y;
        }
    }
    REQUIRE(arrived);
    CHECK(arrivalSpeed > 3.0f);       // he keeps the speed of the fall (D4)
    CHECK(heroFeet(engine).y > 2.0f); // high up in the arrival shaft
    frames(engine, 120);
    REQUIRE(engine.lastLanding().has_value());
    CHECK(engine.lastLanding()->damage == 0.0f); // question 6: no fall damage
    CHECK(run(engine, "stat('hp')").asInteger() == hp);
    CHECK(engine.worldFile().environment.has_value()); // level 2 has no sky either

    // The invisible teleporter back up: level 1 at START_E01_RUECK, the trapdoor as it was left (open).
    run(engine, "teleport(6, 0, 0)");
    for (int i = 0; i < 60 && engine.worldPath() != kLevel1; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    REQUIRE(engine.worldPath() == kLevel1);
    frames(engine, 3);
    CHECK(heroFeet(engine).x == doctest::Approx(-10.0f).epsilon(0.02));
    CHECK(heroFeet(engine).z == doctest::Approx(-10.0f).epsilon(0.02));
    CHECK(open(engine, "TRAP_E01"));
}

TEST_CASE("Dungeon scenario: the resting gatterbock wakes as the hero comes near")
{
    Engine engine(dungeonConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    CHECK(dormant(engine, "mon_gatterbock"));
    frames(engine, 60);
    CHECK(dormant(engine, "mon_gatterbock")); // 18 m away: beyond half its sight
    run(engine, "teleport(0, 0, -9)");        // 9 m in front of it
    frames(engine, 90);
    CHECK_FALSE(dormant(engine, "mon_gatterbock"));
}

TEST_CASE("Dungeon scenario: a save in the middle of the puzzle brings everything back")
{
    std::string saved;
    {
        Engine engine(dungeonConfig());
        REQUIRE(engine.init().ok());
        frames(engine, 3);
        use(engine, "LEVER_E01_TOR");
        REQUIRE(engine.spawnItem("it_apple", 1, Vec3(9.0f, 0.0f, -6.0f)).ok());
        run(engine, "teleport(-4, 0, 0.5)");
        frames(engine, 2);
        use(engine, "BUTTON_E01_STEIN");
        run(engine, "teleport(0, 0, -9)"); // wakes the gatterbock
        frames(engine, 90);
        REQUIRE_FALSE(dormant(engine, "mon_gatterbock"));
        run(engine, "teleport(5, 0, -2)");
        frames(engine, 3);
        REQUIRE(open(engine, "GATE_E01_TOR"));
        REQUIRE(open(engine, "GATE_E01_PLATTE"));
        REQUIRE(open(engine, "WALL_E01_NISCHE"));
        auto state = engine.captureGame();
        REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
        saved = save::toJson(state.value());
    }
    Engine engine(dungeonConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    CHECK_FALSE(open(engine, "GATE_E01_TOR"));
    auto state = save::fromJson(saved);
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    REQUIRE(engine.applyGame(state.value()).ok());
    frames(engine, 3);
    CHECK(open(engine, "GATE_E01_TOR"));
    CHECK(engine.moverInfo("GATE_E01_TOR")->position == 1.0f);
    CHECK(engine.platePressed("PLATE_E01_STEIN")); // the apple lies on it again
    CHECK(open(engine, "GATE_E01_PLATTE"));
    CHECK(open(engine, "WALL_E01_NISCHE"));
    CHECK(run(engine, "switch_state('BUTTON_E01_STEIN').spent").asBool());
    CHECK(run(engine, "switch_state('LEVER_E01_TOR').on").asBool());
    CHECK_FALSE(dormant(engine, "mon_gatterbock"));
}

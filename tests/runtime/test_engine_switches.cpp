// Switches and pressure plates in the engine (docs/modules/dungeon.md D3) on testworld/switches.g7world: a
// lever opens a gate and closes it when thrown back; a hidden once-button is seen from close by only, as a
// loose stone, and opens a secret wall once; a plate holds a gate open while the hero or an item lies on it;
// a player-only plate ignores items; switch state and plates survive a save game.

#include <g7/runtime/Engine.hpp>
#include <g7/save/GameState.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig switchesConfig()
{
    EngineConfig config;
    config.appName = "switches";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/switches.g7world");
    config.start = "START_SCHALTER";
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

std::string focusName(Engine& engine)
{
    engine.updateFocus();
    const auto focus = engine.focus();
    return focus ? focus->name : std::string();
}
} // namespace

TEST_CASE("Engine switches: a lever opens a gate and closes it when thrown back")
{
    Engine engine(switchesConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    frames(engine, 3);
    CHECK_FALSE(open(engine, "GATE_HEBEL"));
    CHECK_FALSE(run(engine, "switch_state('LEVER_HEBEL').on").asBool());
    CHECK(run(engine, "switch_state('LEVER_HEBEL').type").asString() == "lever");
    CHECK(run(engine, "switch_state('NOTHING')").isNil());

    use(engine, "LEVER_HEBEL");
    CHECK(run(engine, "switch_state('LEVER_HEBEL').on").asBool());
    CHECK(open(engine, "GATE_HEBEL"));
    CHECK(engine.moverInfo("GATE_HEBEL")->position == 1.0f);
    use(engine, "LEVER_HEBEL"); // thrown back: the opposite (close)
    CHECK_FALSE(run(engine, "switch_state('LEVER_HEBEL').on").asBool());
    CHECK_FALSE(open(engine, "GATE_HEBEL"));
}

TEST_CASE("Engine switches: a hidden once-button is a loose stone from close by and works one time")
{
    Engine engine(switchesConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    // 2.5 m away an ordinary mob would be in focus, a hidden one is not.
    run(engine, "teleport(-4, 0, 2.5)");
    frames(engine, 2);
    CHECK(focusName(engine).empty());
    run(engine, "teleport(-4, 0, 0.5)");
    frames(engine, 2);
    CHECK(focusName(engine) == engine.uiText("UI_LOOSE_STONE"));
    CHECK_FALSE(run(engine, "switch_state('BUTTON_STEIN').found").asBool());

    use(engine, "BUTTON_STEIN");
    CHECK(open(engine, "WALL_NISCHE"));
    CHECK(run(engine, "switch_state('BUTTON_STEIN').found").asBool());
    CHECK(run(engine, "switch_state('BUTTON_STEIN').spent").asBool());
    run(engine, "teleport(-4, 0, 0.5)");
    frames(engine, 2);
    CHECK(focusName(engine) == "wall_button"); // found: its own name (a bare type here)

    // Once: the second press does nothing (closing it again is up to Lua or another switch).
    run(engine, "send('WALL_NISCHE', 'close')");
    frames(engine, 40);
    CHECK_FALSE(open(engine, "WALL_NISCHE"));
    use(engine, "BUTTON_STEIN");
    CHECK_FALSE(open(engine, "WALL_NISCHE"));
}

TEST_CASE("Engine switches: a plate holds a gate open while the hero or an item is on it")
{
    Engine engine(switchesConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    run(engine, "Story.heard = {} on('dungeon_event', function(target, action, source) "
                "Story.heard[#Story.heard + 1] = target .. ' ' .. action .. ' ' .. source end)");
    // The apple on the player-only plate presses nothing.
    CHECK_FALSE(engine.platePressed("PLATE_HELD"));
    CHECK_FALSE(engine.platePressed("PLATE_STEIN"));

    // The hero steps on and off.
    run(engine, "teleport(6, 0, -6)");
    frames(engine, 3);
    CHECK(engine.platePressed("PLATE_STEIN"));
    CHECK(run(engine, "switch_state('PLATE_STEIN').on").asBool());
    frames(engine, 40);
    CHECK(open(engine, "GATE_PLATTE"));
    run(engine, "teleport(6, 0, -3)");
    frames(engine, 40);
    CHECK_FALSE(engine.platePressed("PLATE_STEIN"));
    CHECK_FALSE(open(engine, "GATE_PLATTE"));

    // An apple put down on it holds it; picked up again, the gate shuts.
    REQUIRE(engine.spawnItem("it_apple", 1, Vec3(6.0f, 0.0f, -6.0f)).ok());
    frames(engine, 40);
    CHECK(engine.platePressed("PLATE_STEIN"));
    CHECK(open(engine, "GATE_PLATTE"));
    run(engine, "teleport(6, 0, -4.6)"); // facing -Z: the apple in front
    frames(engine, 2);
    engine.updateFocus();
    REQUIRE(engine.focus().has_value());
    CHECK(engine.focus()->kind == gameplay::FocusKind::Item);
    REQUIRE(engine.pickUpFocus().ok());
    for (int i = 0; i < 300 && engine.pickingUp(); ++i) // the clip takes ~2 s
    {
        REQUIRE(engine.runFrame());
    }
    frames(engine, 40);
    CHECK(engine.worldItems().size() == 1); // the one on the other plate
    CHECK_FALSE(engine.platePressed("PLATE_STEIN"));
    CHECK_FALSE(open(engine, "GATE_PLATTE"));

    // The player-only plate: on when he steps on, its own release action when he leaves.
    run(engine, "teleport(-6, 0, -6)");
    frames(engine, 3);
    run(engine, "teleport(-6, 0, -3)");
    frames(engine, 3);
    const i64 heard = run(engine, "#Story.heard").asInteger();
    REQUIRE(heard >= 2);
    CHECK(run(engine, std::format("Story.heard[{}]", heard - 1)).asString() == "LUA_HELD on PLATE_HELD");
    CHECK(run(engine, std::format("Story.heard[{}]", heard)).asString() == "LUA_HELD wake PLATE_HELD");
}

TEST_CASE("Engine switches: levers, found and spent buttons and pressed plates come back from a save")
{
    std::string saved;
    {
        Engine engine(switchesConfig());
        REQUIRE(engine.init().ok());
        frames(engine, 3);
        use(engine, "LEVER_HEBEL");
        use(engine, "BUTTON_STEIN");
        REQUIRE(engine.spawnItem("it_apple", 1, Vec3(6.0f, 0.0f, -6.0f)).ok());
        frames(engine, 40);
        REQUIRE(open(engine, "GATE_PLATTE"));
        auto state = engine.captureGame();
        REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
        saved = save::toJson(state.value());
    }
    CHECK(saved.find("\"spent\"") != std::string::npos);

    Engine engine(switchesConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    auto state = save::fromJson(saved);
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    REQUIRE(engine.applyGame(state.value()).ok());
    frames(engine, 3);
    CHECK(run(engine, "switch_state('LEVER_HEBEL').on").asBool());
    CHECK(run(engine, "switch_state('BUTTON_STEIN').found").asBool());
    CHECK(run(engine, "switch_state('BUTTON_STEIN').spent").asBool());
    CHECK(open(engine, "GATE_HEBEL"));
    CHECK(open(engine, "WALL_NISCHE"));
    // The apple lies on the plate again: pressed, quietly (the gate is open from the save).
    CHECK(engine.platePressed("PLATE_STEIN"));
    CHECK(open(engine, "GATE_PLATTE"));
    frames(engine, 40);
    CHECK(open(engine, "GATE_PLATTE"));
}

// The event net in the engine (docs/modules/dungeon.md D1) on testworld/events.g7world: two plates (triggers)
// feed a relay of mode all that opens a door and tells Lua; a once-trigger unlocks a locked door; Lua sends
// events itself; the relay's state and events on their way survive a save game.

#include <g7/runtime/Engine.hpp>
#include <g7/save/GameState.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig eventsConfig()
{
    EngineConfig config;
    config.appName = "events";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/events.g7world");
    config.start = "START_EREIGNISSE";
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

/// The hero steps onto the spot and stays a few steps (the triggers notice him in the fixed step).
void stepOn(Engine& engine, f32 x, f32 z)
{
    run(engine, std::format("teleport({}, 0, {})", x, z));
    frames(engine, 5);
}

bool doorOpen(Engine& engine, std::string_view name)
{
    const auto door = engine.findMob(name);
    REQUIRE(door.has_value());
    return engine.mobInfo(*door)->open;
}
} // namespace

TEST_CASE("Engine events: two plates open a door through a relay of mode all; Lua hears it and sends")
{
    Engine engine(eventsConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    frames(engine, 3);
    REQUIRE(engine.events().isRelay("RELAY_TOR"));
    run(engine, "Story.heard = {} on('dungeon_event', function(target, action, source) "
                "Story.heard[#Story.heard + 1] = target .. ' ' .. action .. ' ' .. source end)");

    // One plate is not enough.
    stepOn(engine, 0.0f, 8.0f);
    CHECK_FALSE(doorOpen(engine, "EVT_TOR"));
    CHECK(run(engine, "relay_state('RELAY_TOR').inputs[1]").asString() == "TRG_PLATTE_A");
    CHECK(run(engine, "#relay_state('RELAY_TOR').inputs").asInteger() == 1);
    CHECK(run(engine, "relay_state('RELAY_TOR').mode").asString() == "all");
    CHECK(run(engine, "relay_state('NOTHING')").isNil());
    // The second one opens the door; Lua hears both targets (the door too).
    stepOn(engine, 0.0f, -8.0f);
    CHECK(doorOpen(engine, "EVT_TOR"));
    CHECK(run(engine, "#Story.heard").asInteger() == 2);
    CHECK(run(engine, "Story.heard[1]").asString() == "EVT_TOR open RELAY_TOR");
    CHECK(run(engine, "Story.heard[2]").asString() == "EVT_HALLE open RELAY_TOR");

    // Lua closes it again (in the next step), and a toggle opens it.
    run(engine, "send('EVT_TOR', 'close')");
    CHECK(doorOpen(engine, "EVT_TOR"));
    frames(engine, 1);
    CHECK_FALSE(doorOpen(engine, "EVT_TOR"));
    CHECK(run(engine, "Story.heard[3]").asString() == "EVT_TOR close lua");
    run(engine, "send('EVT_TOR')"); // trigger: a door toggles
    frames(engine, 1);
    CHECK(doorOpen(engine, "EVT_TOR"));
    CHECK_FALSE(engine.runConsoleLine("send('EVT_TOR', 'explode')").ok());

    // The once-trigger unlocks the locked door; the lock and the lockpick are not needed then.
    const auto locked = engine.findMob("EVT_TUER_ZU");
    REQUIRE(locked.has_value());
    CHECK(engine.mobInfo(*locked)->locked);
    stepOn(engine, -8.0f, 0.0f);
    CHECK_FALSE(engine.mobInfo(*locked)->locked);
    run(engine, "send('EVT_TUER_ZU', 'lock', 'RIEGEL')");
    frames(engine, 1);
    CHECK(engine.mobInfo(*locked)->locked);
}

TEST_CASE("Engine events: the relay's inputs and events on their way are saved and come back")
{
    std::string saved;
    {
        Engine engine(eventsConfig());
        REQUIRE(engine.init().ok());
        frames(engine, 3);
        stepOn(engine, 0.0f, 8.0f);               // one plate of two
        run(engine, "send('EVT_HALLE', 'wake')"); // not handled yet when saved
        auto state = engine.captureGame();
        REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
        const save::WorldState& world = state.value().worlds.back();
        REQUIRE(world.events.relays.size() == 1);
        CHECK(world.events.relays[0].inputs == std::vector<std::string>{"TRG_PLATTE_A"});
        REQUIRE(world.events.pending.size() == 1);
        CHECK(world.events.pending[0].event.target == "EVT_HALLE");
        saved = save::toJson(state.value());
    }
    // A save without the net (older) loads with the world's initial state.
    {
        auto state = save::fromJson(saved);
        REQUIRE(state.ok());
        state.value().worlds.back().events = {};
        const std::string old = save::toJson(state.value());
        CHECK(old.find("\"relays\"") == std::string::npos);
        auto reread = save::fromJson(old);
        REQUIRE(reread.ok());
        CHECK(reread.value().worlds.back().events == world::EventNetSnapshot{});
    }

    Engine engine(eventsConfig());
    REQUIRE(engine.init().ok());
    frames(engine, 3);
    auto state = save::fromJson(saved);
    REQUIRE_MESSAGE(state.ok(), (state.ok() ? "" : state.error().message));
    REQUIRE(engine.applyGame(state.value()).ok());
    CHECK(engine.events().relayState("RELAY_TOR").inputs == std::vector<std::string>{"TRG_PLATTE_A"});
    CHECK(engine.events().pendingCount() == 1);
    run(engine, "Story.heard = {} on('dungeon_event', function(target, action, source) "
                "Story.heard[#Story.heard + 1] = target .. ' ' .. action end)");
    frames(engine, 1);
    CHECK(run(engine, "Story.heard[1]").asString() == "EVT_HALLE wake");
    // The other plate completes what was begun before saving.
    CHECK_FALSE(doorOpen(engine, "EVT_TOR"));
    stepOn(engine, 0.0f, -8.0f);
    CHECK(doorOpen(engine, "EVT_TOR"));
}

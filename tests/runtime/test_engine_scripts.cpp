// Scripts in the engine (M7 part C, headless): game/scripts load with the session, the console runs Lua,
// insert/goto/time/where act on the world, world_loaded and timers reach the scripts, a reload keeps Story.

#include <g7/physics/Character.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig scriptConfig()
{
    EngineConfig config;
    config.appName = "scripts";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER"; // feet at (34, 0, 0), looking west
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

/// Runs a console line that must work; its value.
script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

bool printed(const Engine& engine, std::string_view text)
{
    const auto& lines = engine.consoleLines();
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& l) { return l.find(text) != std::string::npos; });
}
} // namespace

TEST_CASE("Engine scripts: content loads, the console acts on the world")
{
    Engine engine(scriptConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    const script::ScriptVm* vm = engine.scripts();
    REQUIRE(vm != nullptr);
    std::string problems;
    for (const script::ScriptError& e : vm->errors())
    {
        problems += e.text() + "\n";
    }
    CHECK_MESSAGE(vm->errors().empty(), problems);
    CHECK(vm->instancesOf("Item").size() >= 6);
    CHECK(vm->instancesOf("Npc").size() >= 4);
    CHECK(vm->findInstance("Info", "dia_gate_guard_hello") != nullptr);
    // world_loaded reached the scripts.
    CHECK(printed(engine, "Welt geladen: testworld/camp.g7world (Besuch 1)"));
    REQUIRE(engine.player() != nullptr);

    // Expressions show their value; errors show with "!".
    CHECK(run(engine, "1 + 2").asInteger() == 3);
    CHECK(printed(engine, "> 1 + 2"));
    CHECK_FALSE(engine.runConsoleLine("insert('it_dragon')").ok());
    CHECK(printed(engine, "! console:1: insert: unknown instance \"it_dragon\""));

    // insert: items before the player, NPCs as animated figures facing it.
    CHECK(run(engine, "insert('it_apple', 3)").asBool());
    CHECK(run(engine, "insert('it_sword_old')").asBool());
    CHECK(engine.insertedItemCount() == 2);
    CHECK(run(engine, "insert('npc_gate_guard')").asBool());
    CHECK(engine.creatureCount() == 1);
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.creatureState(1) == "move");
    const Vec3 npc = *engine.creaturePosition(1);
    CHECK(npc.x < 34.0f - 2.0f); // west of the player, who looks west
    CHECK(std::abs(npc.y) < 0.05f);

    // teleport (goto is a Lua keyword): start points and coordinates; time; where.
    CHECK(run(engine, "teleport('START_KLETTERPLATZ')").asBool());
    CHECK(engine.player()->feet().x == doctest::Approx(46.0f).epsilon(0.01));
    CHECK(engine.runConsoleLine("teleport(40, 0, -6)").ok());
    CHECK(engine.player()->feet().z == doctest::Approx(-6.0f).epsilon(0.01));
    CHECK_FALSE(engine.runConsoleLine("teleport('NOWHERE')").ok());
    CHECK(engine.runConsoleLine("time(12, 30)").ok());
    CHECK(engine.gameTime().minuteOfDay() == doctest::Approx(750.0).epsilon(0.001));
    CHECK_FALSE(engine.runConsoleLine("time(25)").ok());
    CHECK(run(engine, "where().world").asString() == "testworld/camp.g7world");
    CHECK(run(engine, "where().time").asString() == "12:30");

    // A dialog through the console helper; Story remembers it.
    CHECK(run(engine, "call_info('dia_gate_guard_hello')").asBool());
    CHECK(printed(engine, "Torwache: Halt! Wer bist du?"));
    CHECK_FALSE(run(engine, "call_info('dia_gate_guard_hello')").asBool());
    CHECK(run(engine, "call_info('dia_gate_guard_work')").asBool());
    CHECK(run(engine, "Story.quest_farm_work").asString() == "running");

    // Timers run in simulation time.
    CHECK(engine.runConsoleLine("after(0.1, function() Story.timer_fired = true end)").ok());
    CHECK(run(engine, "Story.timer_fired").isNil());
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(run(engine, "Story.timer_fired").asBool());

    // A reload keeps the story (scripts set Story.visits at load, the old value wins).
    engine.reloadScripts();
    CHECK(run(engine, "Story.met_gate_guard").asBool());
    CHECK(run(engine, "Story.visits").asInteger() == 1);
    CHECK(engine.scripts()->errors().empty());
}

TEST_CASE("Engine scripts: docs/script-api.md lists the engine functions and events")
{
    Engine engine(scriptConfig());
    REQUIRE(engine.init().ok());
    const std::string md = engine.scriptApiMarkdown();
    for (const char* entry :
         {"### `insert(instance: string, count?: integer) -> boolean`", "## Welt", "## Ereignisse",
          "### `on(\"world_loaded\", fn(world: string))`", "### `time(hour: integer, minute?: integer)`"})
    {
        CAPTURE(entry);
        CHECK(md.find(entry) != std::string::npos);
    }
}

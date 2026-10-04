// Dialogues (M10 part A, label "headless"): the gate guard's Infos in the test camp - an important one first,
// the menu of the others in order of `nr`, answers within an Info, Infos told once (or permanent), lines
// skipped, the NPC back to its routine afterwards; an important Info starts by itself when the NPC sees the
// player close by.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig dialogConfig()
{
    EngineConfig config;
    config.appName = "dialog";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

std::string field(Engine& engine, std::string_view name)
{
    return std::string(run(engine, std::format("dialog_state().{}", name)).asString());
}

/// The menu shown, joined with " | " (empty while a line is said).
std::string menu(Engine& engine)
{
    return std::string(run(engine, "table.concat(dialog_state().options, ' | ')").asString());
}

/// Skips every line until the menu shows.
void skipLines(Engine& engine)
{
    for (int i = 0; i < 20 && !field(engine, "line").empty(); ++i)
    {
        engine.dialogSkip();
        REQUIRE(engine.runFrame());
    }
    REQUIRE(engine.runFrame());
}
} // namespace

TEST_CASE("Engine dialogue: important Info first, the menu, answers, Infos told once")
{
    Engine engine(dialogConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(run(engine, "insert_npc('npc_gate_guard', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_gate_guard', '') npc_clear('npc_gate_guard')");
    run(engine, "teleport(9, 0, -4)");
    runSeconds(engine, 0.2f);
    run(engine, "on('dialog_ended', function(npc) Story.ended = npc end)");

    REQUIRE(run(engine, "talk('npc_gate_guard')").asBool());
    CHECK(engine.inDialog());
    // The important greeting runs at once: the guard speaks first, keys count up per Info.
    CHECK(field(engine, "speaker") == "npc_gate_guard");
    CHECK(field(engine, "key") == "dia_gate_guard_hello_00");
    CHECK(field(engine, "line").starts_with("Moment mal."));
    engine.dialogSkip();
    REQUIRE(engine.runFrame());
    CHECK(field(engine, "speaker") == "hero");
    CHECK(field(engine, "key") == "dia_gate_guard_hello_01");
    // Lines also end by themselves (by their length).
    runSeconds(engine, 8.0f);
    CHECK(field(engine, "line").empty());
    CHECK(menu(engine) == "Gibt es hier Arbeit? | Was ist das hier für ein Lager? | Ende");
    CHECK(run(engine, "info_told('dia_gate_guard_hello')").asBool());

    // A topic: the hero asks (its description), the guard answers, then two answers to choose from.
    run(engine, "dialog_choose(1)");
    CHECK(field(engine, "speaker") == "hero");
    CHECK(field(engine, "line") == "Gibt es hier Arbeit?");
    skipLines(engine);
    CHECK(menu(engine) == "Dann gehe ich gleich zu ihr. | Feldarbeit ist nichts für mich.");
    run(engine, "dialog_choose(1)");
    CHECK(field(engine, "line") == "Dann gehe ich gleich zu ihr.");
    skipLines(engine);
    CHECK(run(engine, "Story.quest_farm_work").asString() == "running");
    // Told once: the work topic is gone, the permanent one stays.
    CHECK(menu(engine) == "Was ist das hier für ein Lager? | Ende");
    run(engine, "dialog_choose(1)");
    skipLines(engine);
    CHECK(menu(engine) == "Was ist das hier für ein Lager? | Ende");
    CHECK_FALSE(engine.runConsoleLine("dialog_choose(0)").ok()); // entries count from 1

    // The end: the guard goes back to his routine.
    run(engine, "set_routine('npc_gate_guard', 'rtn_gate_guard_start')");
    run(engine, "dialog_choose(2)");
    runSeconds(engine, 0.2f);
    CHECK_FALSE(engine.inDialog());
    CHECK(run(engine, "Story.ended").asString() == "npc_gate_guard");
    runSeconds(engine, 1.0f);
    CHECK(run(engine, "npc_state('npc_gate_guard').state").asString() == "zs_stand_guarding");
    // Outside a dialogue the dialogue functions are errors.
    CHECK_FALSE(engine.runConsoleLine("say('hero', 'hallo')").ok());
}

TEST_CASE("Engine dialogue: an important Info starts by itself; with approach the NPC walks up first")
{
    Engine engine(dialogConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(run(engine, "insert_npc('npc_gate_guard', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_gate_guard', '') npc_clear('npc_gate_guard')");
    // 8 m away, in his sight: too far to talk, but the greeting has approach = true - he comes.
    run(engine, "teleport(15, 0, -4)");
    run(engine, "npc_turn_to_player('npc_gate_guard')");
    for (int i = 0; i < 60 * 15 && !engine.inDialog(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    REQUIRE(engine.inDialog());
    CHECK(run(engine, "npc_distance_to_player('npc_gate_guard')").asNumber() <= 3.0);
    CHECK(field(engine, "key") == "dia_gate_guard_hello_00");
    engine.endDialog();
    // Told: it does not start again.
    runSeconds(engine, 3.0f);
    CHECK_FALSE(engine.inDialog());
}

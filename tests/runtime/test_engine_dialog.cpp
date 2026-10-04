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
    CHECK(run(engine, "quest_status('quest_farm_work')").asString() == "running"); // in the diary (part D)
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

TEST_CASE("Engine trade: the old man sells at the full value and buys at half, in Gulden")
{
    Engine engine(dialogConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(run(engine, "insert_npc('npc_old_man', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_old_man', '') npc_clear('npc_old_man')");
    run(engine, "teleport(9, 0, -4)");
    run(engine, "give_item('it_gulden', 15) give_item('it_lockpick', 1) give_item('it_sword_old') "
                "equip('it_sword_old')");
    runSeconds(engine, 0.2f);
    REQUIRE(run(engine, "talk('npc_old_man')").asBool());
    REQUIRE(engine.runFrame());
    CHECK(menu(engine) == "Wer bist du? | Zeig mir, was du hast. | Ende");
    run(engine, "dialog_choose(2)");
    skipLines(engine);
    REQUIRE(engine.trading());

    // Buying: a lockpick (value 10) costs 10, an apple (2) costs 2.
    CHECK(run(engine, "trade_price('it_lockpick', true)").asInteger() == 10);
    CHECK(run(engine, "trade_price('it_lockpick', false)").asInteger() == 5);
    const i64 apples = run(engine, "item_count('it_apple')").asInteger();
    CHECK(run(engine, "trade_buy('it_apple')").asBool());
    CHECK(run(engine, "item_count('it_gulden')").asInteger() == 13);
    CHECK(run(engine, "item_count('it_apple')").asInteger() == apples + 1);
    CHECK(run(engine, "npc_item_count('npc_old_man', 'it_gulden')").asInteger() == 122);
    // Not enough Gulden for two lockpicks (20): nothing changes.
    CHECK_FALSE(engine.runConsoleLine("trade_buy('it_lockpick', 2)").ok());
    CHECK(run(engine, "item_count('it_gulden')").asInteger() == 13);
    // Selling the own lockpick brings half its value; the sword in the hand cannot be sold.
    CHECK(run(engine, "trade_sell('it_lockpick')").asBool());
    CHECK(run(engine, "item_count('it_gulden')").asInteger() == 18);
    CHECK(run(engine, "npc_item_count('npc_old_man', 'it_lockpick')").asInteger() == 4);
    CHECK_FALSE(engine.runConsoleLine("trade_sell('it_sword_old')").ok());
    CHECK_FALSE(engine.runConsoleLine("trade_buy('it_gulden', 5)").ok()); // the currency is no ware

    // Closed: the menu again, the topic stays (permanent).
    run(engine, "trade_close()");
    REQUIRE(engine.runFrame());
    CHECK_FALSE(engine.trading());
    CHECK(menu(engine).find("Zeig mir, was du hast.") != std::string::npos);
}

TEST_CASE("Engine teaching: the woodcutter teaches strength for learn points and Gulden")
{
    Engine engine(dialogConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(run(engine, "insert_npc('npc_woodcutter', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_woodcutter', '') npc_clear('npc_woodcutter')");
    run(engine, "teleport(9, 0, -4)");
    run(engine, "give_item('it_gulden', 10) set_learn_points(3)");
    const i64 strength = run(engine, "stat('str')").asInteger();
    runSeconds(engine, 0.2f);
    REQUIRE(run(engine, "talk('npc_woodcutter')").asBool());
    REQUIRE(engine.runFrame());
    CHECK(menu(engine) == "Harte Arbeit, das Holzhacken? | Ende"); // teaching only after meeting him
    run(engine, "dialog_choose(1)");
    skipLines(engine);
    CHECK(menu(engine) == "Bring mir bei, kräftiger zuzupacken. | Ende");
    run(engine, "dialog_choose(1)");
    skipLines(engine);
    CHECK(menu(engine) == "Stärke +1 (1 LP, 5 Gulden) | Stärke +5 (5 LP, 25 Gulden) | Zurück.");
    run(engine, "dialog_choose(1)");
    skipLines(engine);
    CHECK(run(engine, "stat('str')").asInteger() == strength + 1);
    CHECK(run(engine, "hero().learn_points").asInteger() == 2);
    CHECK(run(engine, "item_count('it_gulden')").asInteger() == 5);
    // Too few learn points for +5: he says so, nothing is spent; the offers stay until "Zurück."
    run(engine, "dialog_choose(2)");
    CHECK(field(engine, "line") == "Stärke +5 (5 LP, 25 Gulden)");
    engine.dialogSkip();
    REQUIRE(engine.runFrame());
    CHECK(field(engine, "line") == "Dafür fehlt dir noch die Erfahrung.");
    skipLines(engine);
    CHECK(run(engine, "hero().learn_points").asInteger() == 2);
    run(engine, "dialog_choose(3)");
    skipLines(engine);
    CHECK(menu(engine) == "Bring mir bei, kräftiger zuzupacken. | Ende");
}

TEST_CASE("Engine diary: quests with entries by status, notes by topic, chapters")
{
    Engine engine(dialogConfig());
    REQUIRE(engine.init().ok());
    run(engine, "on('chapter_changed', function(n) Story.changed = n end)");
    CHECK(run(engine, "quest_status('quest_farm_work')").asString() == "none");
    CHECK(run(engine, "quest_start('quest_farm_work')").asBool());
    CHECK_FALSE(run(engine, "quest_start('quest_farm_work')").asBool()); // once
    run(engine, "quest_entry('quest_farm_work', 'Die Bäuerin will, dass ich das Feld umgrabe.')");
    CHECK_FALSE(engine.runConsoleLine("quest_start('quest_nowhere')").ok());
    run(engine, "note('Das Lager', 'Nachts kommen Wölfe bis an den Zaun.')");

    ui::DiaryPanel diary = engine.diaryPanelData();
    CHECK(diary.chapter == "Kapitel 1");
    REQUIRE(diary.running.size() == 1);
    CHECK(diary.running[0].name == "Arbeit im Lager");
    REQUIRE(diary.running[0].entries.size() == 2);
    CHECK(diary.running[0].entries[0] ==
          "Tag 1, 12:00: Die Torwache meint, die Bäuerin am Feld brauche Hilfe.");
    REQUIRE(diary.notes.size() == 1);
    CHECK(diary.notes[0].name == "Das Lager");

    run(engine, "quest_success('quest_farm_work', 'Das Feld ist umgegraben.')");
    diary = engine.diaryPanelData();
    CHECK(diary.running.empty());
    REQUIRE(diary.done.size() == 1);
    CHECK(diary.done[0].entries.size() == 3);

    run(engine, "set_chapter(2, 'Ärger im Lager')");
    CHECK(run(engine, "chapter()").asInteger() == 2);
    CHECK(run(engine, "Story.changed").asInteger() == 2);
    CHECK(engine.diaryPanelData().chapter == "Kapitel 2");
}

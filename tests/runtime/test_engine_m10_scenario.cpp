// M10 milestone B (vertical slice, docs/03-roadmap.md): the camp's placeholder story played through headless
// with the dialogue API - the errand (lunch for the woodcutter), the procurement (a crude sword for the
// guard), the conflict (the farmer woman's ring: bought back, taken by the guard, or pickpocketed), chapter 2
// at the end.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig sliceConfig()
{
    EngineConfig config;
    config.appName = "m10";
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

std::vector<std::string> menu(Engine& engine)
{
    std::vector<std::string> out;
    const script::Value options = run(engine, "dialog_state().options");
    if (const script::Table* t = options.asTable())
    {
        for (const script::Value& v : t->array)
        {
            out.emplace_back(v.asString());
        }
    }
    return out;
}

/// Skips the lines said until the menu shows (or the dialogue ends).
void skipLines(Engine& engine)
{
    for (int i = 0; i < 40 && engine.inDialog() && !run(engine, "dialog_state().line").asString().empty();
         ++i)
    {
        engine.dialogSkip();
        REQUIRE(engine.runFrame());
    }
    REQUIRE(engine.runFrame());
}

/// Chooses the entry with this text from the menu shown.
void choose(Engine& engine, std::string_view text)
{
    skipLines(engine);
    const auto options = menu(engine);
    const auto it = std::ranges::find(options, text);
    REQUIRE_MESSAGE(it != options.end(), "not in the menu: ", std::string(text));
    run(engine, std::format("dialog_choose({})", it - options.begin() + 1));
    skipLines(engine);
}

/// Talks to the NPC, chooses the topics (and answers) in order, ends the dialogue.
void talk(Engine& engine, std::string_view npc, std::initializer_list<std::string_view> path)
{
    REQUIRE(run(engine, std::format("talk('{}')", npc)).asBool());
    REQUIRE(engine.runFrame());
    for (const std::string_view step : path)
    {
        choose(engine, step);
    }
    choose(engine, "Ende");
    REQUIRE_FALSE(engine.inDialog());
}

void setUpCamp(Engine& engine)
{
    for (const char* npc : {"npc_gate_guard", "npc_farmer_woman", "npc_woodcutter", "npc_old_man"})
    {
        REQUIRE(run(engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
        run(engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    }
    run(engine, "teleport(9, 0, -4)");
    run(engine, "Story.met_gate_guard = true"); // the greeting is test_engine_dialog.cpp's
    for (int i = 0; i < 12; ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

/// Up to the ring: work, the errand, the sword.
void firstTwoQuests(Engine& engine)
{
    talk(engine, "npc_gate_guard", {"Gibt es hier Arbeit?", "Dann gehe ich gleich zu ihr."});
    CHECK(run(engine, "quest_status('quest_farm_work')").asString() == "running");

    // Errand: the farmer woman hands over the lunch, the woodcutter gets it.
    talk(engine, "npc_farmer_woman", {"Die Wache schickt mich. Du brauchst Hilfe?"});
    CHECK(run(engine, "quest_status('quest_lunch')").asString() == "running");
    CHECK(run(engine, "item_count('it_lunch')").asInteger() == 1);
    talk(engine, "npc_woodcutter", {"Die Bäuerin schickt dir dein Essen."});
    CHECK(run(engine, "quest_status('quest_lunch')").asString() == "success");
    CHECK(run(engine, "item_count('it_lunch')").asInteger() == 0);
    CHECK(run(engine, "npc_item_count('npc_woodcutter', 'it_lunch')").asInteger() == 1);

    // Procurement: the guard wants a crude sword (forged at the anvil - M8's test covers forging).
    talk(engine, "npc_gate_guard", {"Ich höre, eure Klingen sind stumpf?"});
    CHECK(run(engine, "quest_status('quest_sword')").asString() == "running");
    run(engine, "give_item('it_sword_crude')");
    talk(engine, "npc_gate_guard", {"Hier ist ein Grobes Schwert."});
    CHECK(run(engine, "quest_status('quest_sword')").asString() == "success");

    // Conflict: her ring is gone.
    talk(engine, "npc_farmer_woman", {"Du siehst bedrückt aus."});
    CHECK(run(engine, "quest_status('quest_ring')").asString() == "running");
}

void returnRing(Engine& engine)
{
    REQUIRE(run(engine, "item_count('it_ring_family')").asInteger() == 1);
    talk(engine, "npc_farmer_woman", {"Hier ist dein Ring."});
    CHECK(run(engine, "quest_status('quest_ring')").asString() == "success");
    CHECK(run(engine, "quest_status('quest_farm_work')").asString() == "success");
    CHECK(run(engine, "chapter()").asInteger() == 2);
}
} // namespace

TEST_CASE("M10 scenario: the camp's three quests played through, the ring bought back, chapter 2")
{
    Engine engine(sliceConfig());
    REQUIRE(engine.init().ok());
    setUpCamp(engine);
    const u64 errorsBefore = engine.scripts()->callErrors();
    firstTwoQuests(engine);
    CHECK(run(engine, "item_count('it_gulden')").asInteger() == 40); // 10 for the errand, 30 for the sword

    // Too poor first? No - 40 Gulden: he sells it for 30.
    talk(engine, "npc_old_man",
         {"Du hast einen Ring, der dir nicht gehört.", "Ich kaufe ihn dir ab. (30 Gulden)"});
    CHECK(run(engine, "item_count('it_gulden')").asInteger() == 10);
    returnRing(engine);
    // All in the diary: four quests done, none running.
    const ui::DiaryPanel diary = engine.diaryPanelData();
    CHECK(diary.running.empty());
    CHECK(diary.done.size() == 4);
    CHECK(diary.chapter == "Kapitel 2");
    CHECK(engine.scripts()->callErrors() == errorsBefore);
}

TEST_CASE("M10 scenario: the ring taken back by the guard")
{
    Engine engine(sliceConfig());
    REQUIRE(engine.init().ok());
    setUpCamp(engine);
    firstTwoQuests(engine);
    talk(engine, "npc_old_man",
         {"Du hast einen Ring, der dir nicht gehört.", "Gib ihn her, oder ich hole die Wache."});
    talk(engine, "npc_gate_guard", {"Der alte Mann hat den Ring der Bäuerin."});
    CHECK(run(engine, "npc_item_count('npc_old_man', 'it_ring_family')").asInteger() == 0);
    returnRing(engine);
}

TEST_CASE("M10 scenario: the ring pickpocketed from the old man")
{
    Engine engine(sliceConfig());
    REQUIRE(engine.init().ok());
    run(engine, "Story.met_gate_guard = true");
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    // The old man 2.5 m in front of the hero; talent and dexterity enough (he needs 20).
    REQUIRE(run(engine, "insert('npc_old_man')").asBool());
    run(engine, "set_talent('pickpocket', 1) set_stat('dex', 25)");
    run(engine, "quest_start('quest_ring')"); // the story's way to it is the first test's
    engine.updateFocus();
    REQUIRE(engine.focus().has_value());
    REQUIRE(engine.pickpocketFocus().ok());
    for (int i = 0; i < 400 && engine.pickpocketing(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    // His pickpocket_item comes first: the ring, not an apple from his wares.
    CHECK(run(engine, "item_count('it_ring_family')").asInteger() == 1);
}

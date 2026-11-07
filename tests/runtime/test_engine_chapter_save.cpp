// M15 DoD (milestone C), the part that runs in CI: chapter 1 of the camp played with a save after each
// section, every section going on in a fresh engine that loaded the save - the errand, the blade, the ring,
// chapter 2 (with its autosave) and the letter for Leonberg. Leonberg itself:
// test_engine_leonberg_chapter.cpp (local).

#include "ChapterHelpers.hpp"

#include <g7/runtime/Engine.hpp>
#include <g7/save/SaveFile.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <string>

using namespace g7;
using test::chapterRun;
using test::chapterTalk;

namespace
{
EngineConfig chapterConfig(const std::filesystem::path& saves, bool withWorld)
{
    EngineConfig config;
    config.appName = "chapter";
    config.headless = true;
    if (withWorld)
    {
        config.world = fs::fromUtf8("testworld/camp.g7world");
        config.start = "START_LAGER";
        config.startTime = "12:00";
    }
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.saveDirectory = saves;
    return config;
}

/// Saves into `slot`, then a fresh engine loads it: the next section goes on there.
std::unique_ptr<Engine> saveAndReload(std::unique_ptr<Engine> engine, const std::filesystem::path& saves,
                                      std::string_view slot)
{
    test::chapterSave(*engine, slot);
    engine.reset();
    auto loaded = std::make_unique<Engine>(chapterConfig(saves, false));
    test::chapterLoad(*loaded, slot);
    return loaded;
}
} // namespace

TEST_CASE("M15 DoD: chapter 1 of the camp with a save and a fresh load after every section")
{
    const std::filesystem::path saves = std::filesystem::temp_directory_path() / "g7_chapter_saves";
    std::filesystem::remove_all(saves);
    auto engine = std::make_unique<Engine>(chapterConfig(saves, true));
    REQUIRE(engine->init().ok());
    for (const char* npc : {"npc_gate_guard", "npc_farmer_woman", "npc_woodcutter", "npc_old_man"})
    {
        REQUIRE(chapterRun(*engine, std::format("insert_npc('{}', 'wp_camp_center')", npc)).isString());
        chapterRun(*engine, std::format("set_routine('{}', '') npc_clear('{}')", npc, npc));
    }
    chapterRun(*engine, "teleport(9, 0, -4)");
    chapterRun(*engine, "Story.met_gate_guard = true");
    test::chapterFrames(*engine, 12);

    // Work in the camp, then saved: the next engine knows the quest and the NPCs.
    chapterTalk(*engine, "npc_gate_guard", {"Gibt es hier Arbeit?", "Dann gehe ich gleich zu ihr."});
    engine = saveAndReload(std::move(engine), saves, "slot_01");
    CHECK(chapterRun(*engine, "quest_status('quest_farm_work')").asString() == "running");
    CHECK(engine->creatureCount() == 4);

    // The errand: lunch from the farmer woman to the woodcutter.
    chapterTalk(*engine, "npc_farmer_woman", {"Die Wache schickt mich. Du brauchst Hilfe?"});
    CHECK(chapterRun(*engine, "item_count('it_lunch')").asInteger() == 1);
    engine = saveAndReload(std::move(engine), saves, "slot_02");
    CHECK(chapterRun(*engine, "item_count('it_lunch')").asInteger() == 1); // the inventory came back
    chapterTalk(*engine, "npc_woodcutter", {"Die Bäuerin schickt dir dein Essen."});
    CHECK(chapterRun(*engine, "quest_status('quest_lunch')").asString() == "success");
    CHECK(chapterRun(*engine, "npc_item_count('npc_woodcutter', 'it_lunch')").asInteger() == 1);

    // The blade for the guard (forging is M8's test).
    chapterTalk(*engine, "npc_gate_guard", {"Ich höre, eure Klingen sind stumpf?"});
    chapterRun(*engine, "give_item('it_sword_crude')");
    engine = saveAndReload(std::move(engine), saves, "quick");
    CHECK(chapterRun(*engine, "npc_item_count('npc_woodcutter', 'it_lunch')").asInteger() ==
          1); // NPC inventory
    chapterTalk(*engine, "npc_gate_guard", {"Hier ist ein Grobes Schwert."});
    CHECK(chapterRun(*engine, "quest_status('quest_sword')").asString() == "success");

    // The ring: bought back from the old man, returned - chapter 2, which saves by itself (S3).
    chapterTalk(*engine, "npc_farmer_woman", {"Du siehst bedrückt aus."});
    engine = saveAndReload(std::move(engine), saves, "slot_03");
    chapterTalk(*engine, "npc_old_man",
                {"Du hast einen Ring, der dir nicht gehört.", "Ich kaufe ihn dir ab. (30 Gulden)"});
    chapterTalk(*engine, "npc_farmer_woman", {"Hier ist dein Ring."});
    CHECK(chapterRun(*engine, "chapter()").asInteger() == 2);
    REQUIRE(test::chapterUntil(*engine, [&] { return std::filesystem::exists(saves / "auto.g7save"); }, 120));
    auto autosave = save::readSaveMeta(saves / "auto.g7save");
    REQUIRE(autosave.ok());
    CHECK(autosave.value().chapter == 2);

    // Chapter 2 begins: the guard's letter for Leonberg (the town: test_engine_leonberg_chapter.cpp).
    engine = saveAndReload(std::move(engine), saves, "slot_04");
    CHECK(chapterRun(*engine, "chapter()").asInteger() == 2);
    chapterTalk(*engine, "npc_gate_guard", {"Und wohin jetzt?"});
    CHECK(chapterRun(*engine, "quest_status('quest_leo_letter')").asString() == "running");
    CHECK(chapterRun(*engine, "item_count('it_letter_council')").asInteger() == 1);
    engine = saveAndReload(std::move(engine), saves, "slot_05");
    CHECK(chapterRun(*engine, "item_count('it_letter_council')").asInteger() == 1);
    const Engine::DiaryData diary = engine->diaryData();
    CHECK(diary.chapter == "Kapitel 2");
    CHECK(diary.done.size() == 4);
    CHECK(diary.running.size() == 1);
    CHECK(engine->scripts()->callErrors() == 0);
    engine.reset();
    std::filesystem::remove_all(saves);
}

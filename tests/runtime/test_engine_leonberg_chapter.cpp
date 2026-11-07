// M15 DoD (milestone C), Leonberg (label "headless", with the generated town; not in CI): chapter 2 "Ankunft
// in Leonberg" - from the camp along the road (level change TRG_TO_LEONBERG, autosave) to the Lower Gate, the
// letter to Matthis the council runner, back to the camp through the gate guard and the camp as it was left;
// a fresh engine loads the save made in the town.

#include "ChapterHelpers.hpp"
#include "RunTempDir.hpp"

#include <g7/runtime/Engine.hpp>
#include <g7/save/SaveFile.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

using namespace g7;
using test::chapterRun;
using test::chapterTalk;

namespace
{
const std::string kGenerated =
    std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/generated/leonberg_terrain.r16";

EngineConfig config(const std::filesystem::path& saves, bool withWorld)
{
    EngineConfig c;
    c.appName = "leonberg_chapter";
    c.headless = true;
    if (withWorld)
    {
        c.world = fs::fromUtf8("testworld/camp.g7world");
        c.start = "START_LAGER";
        c.startTime = "10:00";
    }
    c.fixedFrameSeconds = 1.0 / 60.0;
    c.saveDirectory = saves;
    return c;
}
} // namespace

TEST_CASE("M15 DoD: chapter 2 in Leonberg with saves, the way there and back (with the generated town)")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    const std::filesystem::path saves = g7::test::runTempDir() / "g7_leonberg_chapter_saves";
    std::filesystem::remove_all(saves);
    {
        Engine engine(config(saves, true));
        REQUIRE(engine.init().ok());
        // Chapter 1 is test_engine_chapter_save.cpp: here it is done, the guard hands over the letter.
        chapterRun(engine, "Story.chapter = 2 Story.met_gate_guard = true");
        REQUIRE(chapterRun(engine, "insert_npc('npc_gate_guard', 'wp_camp_center')").isString());
        chapterRun(engine, "set_routine('npc_gate_guard', '') npc_clear('npc_gate_guard')");
        chapterRun(engine, "npc_set_stat('npc_gate_guard', 'hp', 33)");
        test::chapterFrames(engine, 10);
        chapterTalk(engine, "npc_gate_guard", {"Und wohin jetzt?"});
        REQUIRE(chapterRun(engine, "item_count('it_letter_council')").asInteger() == 1);

        // Along the road east: the level change to the Lower Gate, an autosave on arrival (S3).
        chapterRun(engine, "teleport(54, 0.2, 0)");
        REQUIRE(test::chapterUntil(
            engine, [&] { return engine.worldPath() == "worlds/leonberg/leonberg.g7world"; }, 120));
        REQUIRE(
            test::chapterUntil(engine, [&] { return std::filesystem::exists(saves / "auto.g7save"); }, 600));
        CHECK(save::readSaveMeta(saves / "auto.g7save").value().world == "worlds/leonberg/leonberg.g7world");
        CHECK(engine.keptWorlds() == 1); // the camp waits as it was

        // The letter to Matthis the council runner.
        const auto gulden = chapterRun(engine, "item_count('it_gulden')").asInteger();
        chapterTalk(engine, "npc_leo_citizen", {"Ich bringe einen Brief aus dem Lager."});
        CHECK(chapterRun(engine, "quest_status('quest_leo_letter')").asString() == "success");
        CHECK(chapterRun(engine, "item_count('it_gulden')").asInteger() == gulden + 50);
        CHECK(chapterRun(engine, "npc_item_count('npc_leo_citizen', 'it_letter_council')").asInteger() == 1);
        test::chapterSave(engine, "slot_01");

        // Back to the camp through the guard at the Lower Gate: the camp as it was left.
        chapterTalk(engine, "npc_leo_guard_lower", {"Ich will zurück zum Lager am Waldrand."}, false);
        REQUIRE(
            test::chapterUntil(engine, [&] { return engine.worldPath() == "testworld/camp.g7world"; }, 120));
        CHECK(chapterRun(engine, "npc_stat('npc_gate_guard', 'hp')").asInteger() == 33);
        CHECK(engine.keptWorlds() == 1); // now Leonberg
        CHECK(engine.scripts()->callErrors() == 0);
    }

    // A fresh engine loads the save made in Leonberg.
    Engine engine(config(saves, false));
    test::chapterLoad(engine, "slot_01");
    CHECK(engine.worldPath() == "worlds/leonberg/leonberg.g7world");
    CHECK(chapterRun(engine, "quest_status('quest_leo_letter')").asString() == "success");
    CHECK(chapterRun(engine, "npc_item_count('npc_leo_citizen', 'it_letter_council')").asInteger() == 1);
    CHECK(chapterRun(engine, "item_count('it_letter_council')").asInteger() == 0);
    CHECK(engine.keptWorlds() == 1); // the camp
    std::filesystem::remove_all(saves);
}

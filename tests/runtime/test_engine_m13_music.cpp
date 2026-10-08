// M13 milestone (DoD scenario F, docs/03-roadmap.md, label "headless"): the music changes cleanly when the
// hero enters the camp, in danger and in a fight - outside every music box silence (owner), in the camp LAGER
// at once; a bandit threatening the hero brings the common threat music on the next bar of the camp's, his
// attack the fight music on the next bar of the threat's; 5 s after the fight the camp music returns at the
// end of the fight segment; leaving the camp it falls silent on a bar boundary. Without a device the frames
// drive the mixer.

#include <g7/audio/Music.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
constexpr u64 kRate = 48000;
constexpr u64 kLagerBar = kRate * 5 / 2; // 96 BPM, 4 beats: 2.5 s
constexpr u64 kThreatBar = kRate * 3;    // 80 BPM: 3 s
constexpr u64 kFightSegment = 307200;    // 4 bars at 150 BPM: 6.4 s

EngineConfig musicConfig()
{
    EngineConfig config;
    config.appName = "m13_music";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER"; // (34, 0, 0): east of the camp's music box
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    // Only the music is heard: the mixer's peak tells music from silence.
    for (const char* bus : {"audio.effects", "audio.voice", "audio.ambient", "audio.ui"})
    {
        config.settings.set<f64>(bus, 0.0);
    }
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

const audio::MusicPlayer::Status& music(Engine& engine)
{
    REQUIRE(engine.music() != nullptr);
    return engine.music()->status();
}

/// Runs until the music plays `set` (at most `limit` seconds); the seconds it took.
f32 waitFor(Engine& engine, std::string_view set, f32 limit)
{
    f32 t = 0.0f;
    while (music(engine).set != set && t < limit)
    {
        runSeconds(engine, 0.1f);
        t += 0.1f;
    }
    return t;
}
} // namespace

TEST_CASE("M13 scenario F: the music changes cleanly entering the camp, in danger, in a fight and leaving")
{
    Engine engine(musicConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(engine.music() != nullptr);
    run(engine, "set_stat('hp_max', 1000) set_stat('hp', 1000)"); // the bandit must not knock him out
    runSeconds(engine, 1.0f);
    CHECK(music(engine).set.empty()); // outside the boxes: silence
    CHECK(run(engine, "music_state().theme").asString().empty());

    // Into the camp: its music at once.
    run(engine, "teleport(8, 0, 4)");
    CHECK(waitFor(engine, "LAGER/std.day", 1.0f) < 0.5f);
    CHECK(music(engine).segment == "music/lager_std_day.wav");
    runSeconds(engine, 1.0f);

    // A bandit threatens the hero: the threat music on the camp music's next bar.
    REQUIRE(run(engine, "insert_npc('npc_bandit', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_bandit', '') npc_teleport('npc_bandit', 8, 0, 10, 0)");
    const u64 campStart = music(engine).segmentStart;
    run(engine, "npc_start_state('npc_bandit', 'zs_threaten')");
    CHECK(waitFor(engine, "common/thr", 2.0f) < 2.0f);
    CHECK(run(engine, "music_state().state").asString() == "thr");
    REQUIRE(music(engine).changeAt > campStart);
    CHECK((music(engine).changeAt - campStart) % kLagerBar == 0); // on a bar boundary
    CHECK(music(engine).changeAt - campStart <= 4 * kLagerBar);
    runSeconds(engine, 3.0f); // the threat music sounds

    // He attacks: the fight music on the threat music's next bar.
    const u64 threatStart = music(engine).segmentStart;
    run(engine, "fight('npc_bandit', 'hero')");
    CHECK(waitFor(engine, "common/fgt", 2.0f) < 2.0f);
    CHECK(run(engine, "music_state().state").asString() == "fgt");
    REQUIRE(music(engine).changeAt >= threatStart);
    CHECK((music(engine).changeAt - threatStart) % kThreatBar == 0);
    const u64 fightStart = music(engine).changeAt;
    runSeconds(engine, 4.0f);
    REQUIRE(engine.audio() != nullptr);
    CHECK(engine.audio()->lastPeak() > 0.05f); // the fight music sounds

    // The fight ends (the bandit calms down); 5 s on the fight music still plays (hysteresis), then the camp
    // music returns where the fight segment ends.
    run(engine, "npc_start_state('npc_bandit', 'zs_stand')");
    runSeconds(engine, 3.0f);
    CHECK(music(engine).set == "common/fgt");
    const f32 calm = waitFor(engine, "LAGER/std.day", 20.0f);
    CHECK(calm < 20.0f);
    REQUIRE(music(engine).changeAt != 0);
    // The change waits for the end of a fight segment (they follow one another from fightStart).
    CHECK(music(engine).changeAt == music(engine).segmentStart);
    CHECK((music(engine).changeAt - fightStart) % kFightSegment == 0);
    runSeconds(engine, 8.0f);
    CHECK(music(engine).set == "LAGER/std.day");

    // A stinger: the hero rises a level.
    run(engine, "add_xp(100000)");
    runSeconds(engine, 0.2f);
    CHECK(run(engine, "music_state().stinger").asString() == "level_up");

    // Out of the camp: silence on a bar boundary of the camp music.
    const u64 lastStart = music(engine).segmentStart;
    run(engine, "teleport(34, 0, 0)");
    runSeconds(engine, 0.3f);
    CHECK(music(engine).set.empty());
    REQUIRE(music(engine).changeAt > 0);
    CHECK((music(engine).changeAt - lastStart) % kLagerBar == 0);
    runSeconds(engine, 3.0f);
    REQUIRE(engine.audio() != nullptr);
    CHECK(engine.audio()->lastPeak() < 1e-3f); // silence
}

TEST_CASE("M13 scenario F: outside the boxes a fight still has its music (owner), then silence again")
{
    Engine engine(musicConfig());
    REQUIRE(engine.init().ok());
    run(engine, "set_stat('hp_max', 1000) set_stat('hp', 1000)");
    run(engine, "music_force(nil, 'fgt')"); // the zone stays the world's (none), the state forced
    runSeconds(engine, 0.5f);
    CHECK(music(engine).set == "common/fgt");
    run(engine, "music_force(nil, 'std')");
    runSeconds(engine, 2.0f); // silent from the fight music's next bar (1.6 s)
    CHECK(music(engine).set.empty());
    run(engine, "music_force()");
    CHECK_FALSE(run(engine, "music_stinger('quest')").isNil());
}

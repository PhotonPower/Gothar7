// Dynamic music (M13 part C): definitions and their errors, the choice of a set with its fallbacks, the
// state's hysteresis, and the player on the mixer's clock - segments chained sample exact, a change on the
// next bar boundary or at the segment's end, silence outside the zones.

#include <g7/audio/Music.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::audio;

namespace
{
constexpr u32 kRate = 48000;

/// A mono 16-bit WAV at the mixer's rate (no resampling: frames stay exact).
std::vector<u8> sineWav(f32 seconds, f32 hz)
{
    const u32 frames = static_cast<u32>(seconds * static_cast<f32>(kRate));
    std::vector<u8> out;
    const auto put = [&](const void* p, usize n)
    { out.insert(out.end(), static_cast<const u8*>(p), static_cast<const u8*>(p) + n); };
    const auto u32le = [&](u32 v) { put(&v, 4); };
    const auto u16le = [&](u16 v) { put(&v, 2); };
    put("RIFF", 4);
    u32le(36 + frames * 2);
    put("WAVEfmt ", 8);
    u32le(16);
    u16le(1);
    u16le(1);
    u32le(kRate);
    u32le(kRate * 2);
    u16le(2);
    u16le(16);
    put("data", 4);
    u32le(frames * 2);
    for (u32 i = 0; i < frames; ++i)
    {
        const auto s = static_cast<i16>(16383.0 * std::sin(6.283185307 * hz * i / kRate));
        u16le(static_cast<u16>(s));
    }
    return out;
}

constexpr std::string_view kMusic = R"(
threat_states = ["zs_threaten"]
outside = ["fgt"]
fight_states = ["zs_attack"]
hysteresis = 5.0

[theme.T]
bpm = 120          # a bar of 4 beats: 2 s
beats = 4
[theme.T.std.day]
segments = ["m/std_a.wav", "m/std_b.wav"]
[theme.T.std.ngt]
segments = ["m/std_n.wav"]
[theme.T.thr]
segments = ["m/thr.wav"]
transition = "end"

[theme.common]
bpm = 120
[theme.common.fgt]
segments = ["m/fgt.wav"]
fade = 0.2
bpm = 150          # its own tempo: a bar of 1.6 s

[stinger.quest]
files = ["m/quest.wav"]
)";

MusicDefs defs()
{
    auto parsed = parseMusicDefs(kMusic, "music.toml");
    REQUIRE_MESSAGE(parsed.ok(), (parsed.ok() ? "" : parsed.error().message));
    return std::move(parsed).value();
}

AudioSystem mixerWithMusic()
{
    auto created = AudioSystem::create({.device = false, .sampleRate = kRate, .seed = 1});
    REQUIRE(created.ok());
    AudioSystem audio = std::move(created).value();
    // Segments of 2 bars (4 s); the stinger 0.5 s.
    for (const char* f : {"m/std_a.wav", "m/std_b.wav", "m/std_n.wav", "m/thr.wav", "m/fgt.wav"})
    {
        REQUIRE(audio.addClip(f, sineWav(4.0f, 440.0f)).ok());
    }
    REQUIRE(audio.addClip("m/quest.wav", sineWav(0.5f, 880.0f)).ok());
    return audio;
}

/// Renders `seconds` in steps of 0.05 s, the player updated before each.
void play(AudioSystem& audio, MusicPlayer& music, f32 seconds, std::string_view theme, MusicState state,
          bool night = false)
{
    for (int i = 0; i < static_cast<int>(std::lround(seconds / 0.05f)); ++i)
    {
        music.update(audio, theme, state, night);
        audio.update(0.05f);
    }
}
} // namespace

TEST_CASE("music: definitions, their errors and the choice with its fallbacks")
{
    const MusicDefs d = defs();
    CHECK(d.themes.size() == 2);
    CHECK(d.themes.at("T").barSeconds() == doctest::Approx(2.0));
    CHECK(d.themes.at("T").sets.at("thr").transition == MusicTransition::End);
    CHECK(d.themes.at("common").sets.at("fgt").fade == doctest::Approx(0.2f));
    CHECK(d.threatStates == std::vector<std::string>{"zs_threaten"});
    CHECK(d.hysteresis == doctest::Approx(5.0f));
    MusicPlayer player(d);
    CHECK(player.files().size() == 6);

    const auto id = [&](std::string_view theme, MusicState s, bool night)
    {
        const auto c = chooseMusic(d, theme, s, night);
        return c ? c->id() : std::string("-");
    };
    CHECK(id("T", MusicState::Std, false) == "T/std.day");
    CHECK(id("T", MusicState::Std, true) == "T/std.ngt");
    CHECK(id("T", MusicState::Thr, true) == "T/thr");       // no time of day: both
    CHECK(id("T", MusicState::Fgt, false) == "common/fgt"); // shared fight music
    CHECK(id("", MusicState::Std, false) == "-");           // outside the zones: silence (owner)
    CHECK(id("", MusicState::Thr, false) == "-");           // ... threat too (not in `outside`)
    CHECK(id("", MusicState::Fgt, true) == "common/fgt");   // ... but the fight (`outside`)
    CHECK(id("LAND", MusicState::Std, false) == "-");       // a zone without music: as outside
    CHECK(id("LAND", MusicState::Fgt, false) == "common/fgt");
    CHECK(id("common", MusicState::Std, false) == "-"); // nothing for std in common
    CHECK(d.outside == std::vector<MusicState>{MusicState::Fgt});
    CHECK_FALSE(parseMusicDefs("outside = [\"calm\"]\n", "x").ok());
    CHECK(chooseMusic(d, "T", MusicState::Fgt, false)->barSeconds() == doctest::Approx(1.6));
    CHECK(chooseMusic(d, "T", MusicState::Std, false)->barSeconds() == doctest::Approx(2.0));

    // A state without music falls back to the next lower one.
    auto fallback = parseMusicDefs("[theme.X.std]\nsegments = [\"a.wav\"]\n", "x");
    REQUIRE(fallback.ok());
    CHECK(chooseMusic(fallback.value(), "X", MusicState::Fgt, true)->id() == "X/std");

    CHECK_FALSE(parseMusicDefs("[theme.X.std]\nintro = \"a.wav\"\n", "x").ok()); // segments missing
    CHECK_FALSE(parseMusicDefs("[theme.X.calm]\nsegments = [\"a.wav\"]\n", "x").ok());
    CHECK_FALSE(parseMusicDefs("[theme.X.std.dawn]\nsegments = [\"a.wav\"]\n", "x").ok());
    CHECK_FALSE(parseMusicDefs("[theme.X]\nbpm = 0\n[theme.X.std]\nsegments = [\"a.wav\"]\n", "x").ok());
    CHECK_FALSE(parseMusicDefs("[theme.X.std]\nsegments = [\"a.wav\"]\ntransition = \"now\"\n", "x").ok());
    CHECK_FALSE(parseMusicDefs("[theme.X]\nbpm = 90\n", "x").ok()); // no music at all
    CHECK_FALSE(parseMusicDefs("[stinger.s]\nvolume = 1.0\n", "x").ok());
    CHECK(musicStateFromName("fgt") == MusicState::Fgt);
    CHECK_FALSE(musicStateFromName("calm").has_value());
}

TEST_CASE("music: the state goes up at once and down only after the hysteresis")
{
    MusicStateFilter f;
    CHECK(f.update(MusicState::Fgt, 0.1f, 5.0f) == MusicState::Fgt);
    CHECK(f.update(MusicState::Std, 4.0f, 5.0f) == MusicState::Fgt);
    CHECK(f.update(MusicState::Fgt, 0.1f, 5.0f) == MusicState::Fgt); // the fight again: the clock restarts
    CHECK(f.update(MusicState::Std, 4.9f, 5.0f) == MusicState::Fgt);
    CHECK(f.update(MusicState::Thr, 0.2f, 5.0f) == MusicState::Thr); // 5 s calm: straight to what is
    CHECK(f.update(MusicState::Std, 5.0f, 5.0f) == MusicState::Std);
}

TEST_CASE(
    "music: segments chain sample exact; a change waits for the next bar, \"end\" for the segment's end")
{
    AudioSystem audio = mixerWithMusic();
    MusicPlayer music(defs(), 3);
    const u64 bar = 2 * kRate;
    const u64 segment = 4 * kRate;

    music.update(audio, "T", MusicState::Std, false);
    const u64 first = music.status().segmentStart;
    CHECK(music.status().set == "T/std.day");
    CHECK(first > 0);
    CHECK(first < kRate / 10); // at once (50 ms ahead)
    const std::string firstFile = music.status().segment;

    // Chained: the next segment starts exactly where the first ends, and it is the other one.
    play(audio, music, 4.5f, "T", MusicState::Std);
    CHECK(music.status().segmentStart == first + segment);
    CHECK(music.status().segment != firstFile);
    CHECK(audio.lastPeak() > 0.1f);

    // The fight: on the next bar of the running segment (at 4.55 s it started 0.55 s ago -> 1.45 s on).
    const u64 running = music.status().segmentStart;
    music.update(audio, "T", MusicState::Fgt, false);
    CHECK(music.status().set == "common/fgt");
    CHECK(music.status().changeAt == running + bar);
    CHECK(music.status().segmentStart == running + bar);
    play(audio, music, 2.0f, "T", MusicState::Fgt);
    CHECK(music.status().changeAt == 0);
    CHECK(audio.lastPeak() > 0.1f);

    // Threat waits for the end of the fight segment ("end").
    const u64 fight = music.status().segmentStart;
    music.update(audio, "T", MusicState::Thr, false);
    CHECK(music.status().changeAt == fight + segment);
    CHECK(music.status().set == "T/thr");

    // Changing the mind before the change: the planned segment never sounds, the new one takes its place.
    music.update(audio, "T", MusicState::Fgt, false);
    CHECK(music.status().set == "common/fgt");
    CHECK(music.status().segmentStart == fight + segment); // where the threat would have begun
}

TEST_CASE("music: outside the zones it falls silent on a bar boundary; night music; stingers")
{
    AudioSystem audio = mixerWithMusic();
    MusicPlayer music(defs(), 1);
    play(audio, music, 1.0f, "T", MusicState::Std, true);
    CHECK(music.status().set == "T/std.ngt");
    const u64 start = music.status().segmentStart;
    music.update(audio, "", MusicState::Std, true);
    CHECK(music.status().set.empty());
    CHECK(music.status().changeAt == start + 2 * kRate);
    play(audio, music, 1.5f, "", MusicState::Std, true);
    CHECK(audio.lastPeak() < 1e-4f);
    CHECK(audio.playingCount() == 0);

    // Back into the zone: music again at once.
    music.update(audio, "T", MusicState::Std, false);
    CHECK(music.status().set == "T/std.day");
    REQUIRE(music.stinger(audio, "quest").ok());
    CHECK_FALSE(music.stinger(audio, "unknown").ok());
    play(audio, music, 1.0f, "T", MusicState::Std);
    CHECK(audio.playingCount() >= 2); // the segment, the next one queued (the stinger may have ended)

    music.stop(audio, 0.1f);
    CHECK(music.status().set.empty());
    play(audio, music, 0.0f, "", MusicState::Std);
    audio.update(0.2f);
    CHECK(audio.playingCount() == 0);
}

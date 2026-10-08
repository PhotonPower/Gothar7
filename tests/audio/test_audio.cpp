// audio (M13 part A, ADR 0007): sound definitions as data; the mixer without a device - clips decoded, sounds
// played to their end, buses and the master turning them down, 3D distance, a delayed start on the mixer's
// clock.

#include <g7/audio/Audio.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::audio;

namespace
{
/// A mono 16-bit WAV: a sine of `hz` for `seconds` at full scale (amplitude 0.5).
std::vector<u8> sineWav(f32 seconds, f32 hz = 440.0f, u32 rate = 22050)
{
    const u32 frames = static_cast<u32>(seconds * static_cast<f32>(rate));
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
    u32le(rate);
    u32le(rate * 2);
    u16le(2);
    u16le(16);
    put("data", 4);
    u32le(frames * 2);
    for (u32 i = 0; i < frames; ++i)
    {
        const auto s = static_cast<i16>(16383.0 * std::sin(6.283185307 * hz * i / rate));
        u16le(static_cast<u16>(s));
    }
    return out;
}

AudioSystem mixer()
{
    auto created = AudioSystem::create({.device = false, .sampleRate = 48000, .seed = 1});
    REQUIRE_MESSAGE(created.ok(), (created.ok() ? "" : created.error().message));
    return std::move(created).value();
}
} // namespace

TEST_CASE("audio: sound definitions and their errors")
{
    auto defs = parseSoundDefs(R"(
[anvil_hit]
files = ["sounds/anvil_hit_1.wav", "sounds/anvil_hit_2.wav"]
volume = 0.8
pitch_jitter = 0.1
max_distance = 25
[camp_loop]
files = ["sounds/camp.ogg"]
bus = "ambient"
loop = true
)",
                               "sounds.toml");
    REQUIRE_MESSAGE(defs.ok(), (defs.ok() ? "" : defs.error().message));
    REQUIRE(defs.value().size() == 2);
    const SoundDef& anvil = defs.value().at("anvil_hit");
    CHECK(anvil.files.size() == 2);
    CHECK(anvil.volume == doctest::Approx(0.8f));
    CHECK(anvil.bus == Bus::Effects);
    CHECK(defs.value().at("camp_loop").loop);
    CHECK(defs.value().at("camp_loop").bus == Bus::Ambient);

    const auto fails = [](std::string_view toml, std::string_view expected)
    {
        auto r = parseSoundDefs(toml, "bad.toml");
        REQUIRE_FALSE(r.ok());
        CHECK_MESSAGE(r.error().message.find(expected) != std::string::npos, r.error().message);
    };
    fails("[x]\nvolume = 1\n", "no files");
    fails("[x]\nfiles = [\"a.wav\"]\nbus = \"radio\"\n", "unknown bus");
    fails("[x]\nfiles = [\"a.wav\"]\nmin_distance = 5\nmax_distance = 2\n", "min_distance");
    CHECK(busFromName("voice") == Bus::Voice);
    CHECK(busName(Bus::Music) == "music");
}

TEST_CASE("audio: a clip plays to its end; buses and the master turn it down; a broken file is refused")
{
    AudioSystem audio = mixer();
    const auto wav = sineWav(0.5f);
    REQUIRE(audio.addClip("sounds/beep.wav", wav).ok());
    CHECK(audio.hasClip("sounds/beep.wav"));
    CHECK(audio.clipSeconds("sounds/beep.wav") == doctest::Approx(0.5f).epsilon(0.01));
    const std::vector<u8> junk = {1, 2, 3, 4, 5};
    CHECK_FALSE(audio.addClip("sounds/junk.wav", junk).ok());

    SoundDef beep;
    beep.files = {"sounds/beep.wav"};
    auto id = audio.play(beep);
    REQUIRE(id.ok());
    CHECK(audio.playing(id.value()));
    audio.update(0.2f);
    CHECK(audio.lastPeak() > 0.3f); // the sine (0.5) comes through
    CHECK(audio.time() == doctest::Approx(0.2).epsilon(0.01));
    audio.update(0.5f);
    CHECK_FALSE(audio.playing(id.value())); // played to its end and freed
    CHECK(audio.playingCount() == 0);

    // The effects bus at zero: silent; the music bus untouched.
    audio.setBusVolume(Bus::Effects, 0.0f);
    REQUIRE(audio.play(beep).ok());
    audio.update(0.1f);
    CHECK(audio.lastPeak() < 1e-4f);
    SoundDef music = beep;
    music.bus = Bus::Music;
    REQUIRE(audio.play(music).ok());
    audio.update(0.1f);
    CHECK(audio.lastPeak() > 0.3f);
    audio.setMasterVolume(0.0f);
    audio.update(0.1f);
    CHECK(audio.lastPeak() < 1e-4f);

    SoundDef missing;
    missing.files = {"sounds/nowhere.wav"};
    CHECK_FALSE(audio.play(missing).ok());
}

TEST_CASE(
    "audio: 3D - quieter with distance, silent beyond the maximum; a loop until stopped; a delayed start")
{
    AudioSystem audio = mixer();
    REQUIRE(audio.addClip("sounds/beep.wav", sineWav(0.5f)).ok());
    audio.setListener(Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f));
    SoundDef beep;
    beep.files = {"sounds/beep.wav"};
    beep.minDistance = 1.0f;
    beep.maxDistance = 20.0f;
    beep.loop = true;
    const auto level = [&](const Vec3& at)
    {
        auto id = audio.play(beep, at);
        REQUIRE(id.ok());
        audio.update(0.1f); // the gain ramps in
        audio.update(0.1f);
        const f32 peak = audio.lastPeak();
        audio.stop(id.value());
        audio.update(0.05f);
        return peak;
    };
    const f32 near = level(Vec3(0.0f, 0.0f, -1.0f));
    const f32 middle = level(Vec3(0.0f, 0.0f, -10.0f));
    const f32 far = level(Vec3(0.0f, 0.0f, -30.0f));
    CHECK(near > 0.2f);
    CHECK(middle < near * 0.8f);
    CHECK(middle > 0.01f);
    CHECK(far < 1e-3f);

    // A loop goes on past its length until stopped; faded out it ends.
    auto loop = audio.play(beep);
    REQUIRE(loop.ok());
    audio.update(1.2f);
    CHECK(audio.playing(loop.value()));
    audio.stop(loop.value(), 0.2f);
    audio.update(0.3f);
    CHECK_FALSE(audio.playing(loop.value()));

    // Delayed by 0.5 s on the mixer's clock: silent first, then heard.
    beep.loop = false;
    auto later = audio.play(beep, std::nullopt, 0.5f);
    REQUIRE(later.ok());
    audio.update(0.4f);
    CHECK(audio.lastPeak() < 1e-4f);
    audio.update(0.3f);
    CHECK(audio.lastPeak() > 0.3f);
}

TEST_CASE("audio: occlusion - a muffled 3D sound loses its highs")
{
    AudioSystem audio = mixer();
    REQUIRE(audio.addClip("sounds/high.wav", sineWav(1.0f, 6000.0f)).ok());
    audio.setListener(Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f));
    SoundDef high;
    high.files = {"sounds/high.wav"};
    high.loop = true;
    auto id = audio.play(high, Vec3(0.0f, 0.0f, -1.0f));
    REQUIRE(id.ok());
    audio.update(0.2f);
    audio.update(0.1f);
    const f32 open = audio.lastPeak();
    audio.setMuffle(id.value(), 1.0f);
    CHECK(audio.muffle(id.value()) == doctest::Approx(1.0f));
    audio.update(0.1f);
    audio.update(0.1f);
    CHECK(audio.lastPeak() < open * 0.2f); // 6 kHz through a cut at about 600 Hz
    audio.setMuffle(id.value(), 0.0f);
    audio.update(0.1f);
    audio.update(0.1f);
    CHECK(audio.lastPeak() > open * 0.8f);
}

TEST_CASE("audio: ambiences as data and their errors")
{
    auto defs = parseAmbientDefs(R"(
[camp]
loop = "amb_camp"
loop_night = "amb_night"
randoms = ["bird"]
randoms_night = ["owl", "cricket"]
interval = [8, 20]
distance = [5.5, 15]
[wind]
loop = "amb_wind"
)",
                                 "ambient.toml");
    REQUIRE_MESSAGE(defs.ok(), (defs.ok() ? "" : defs.error().message));
    const AmbientDef& camp = defs.value().at("camp");
    CHECK(camp.loopNight == "amb_night");
    CHECK(camp.randomsNight.size() == 2);
    CHECK(camp.intervalMin == doctest::Approx(8.0f));
    CHECK(camp.intervalMax == doctest::Approx(20.0f));
    CHECK(camp.distanceMin == doctest::Approx(5.5f));
    CHECK(defs.value().at("wind").randoms.empty());
    CHECK_FALSE(parseAmbientDefs("[x]\nfade = 1\n", "a").ok());
    CHECK_FALSE(parseAmbientDefs("[x]\nloop = \"a\"\ninterval = [20, 8]\n", "a").ok());
}

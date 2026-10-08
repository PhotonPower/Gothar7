// Voices (M13 part D, label "headless"): a dialogue line with its take (a synthetic WAV mounted from a
// temporary folder - no placeholder voices in the repository, owner) is spoken: the line lasts as long as the
// take plus a pause, the speaker's mouth (vis_aa) follows the loudness - open in the sound, closed in the gap
// - the music ducks by 6 dB; skipping stops the voice; a line without a take keeps its reading time.

#include <g7/audio/Audio.hpp>
#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream> // doctest needs it to print std::string operands
#include <random>
#include <string>
#include <vector>

using namespace g7;

namespace
{
constexpr u32 kRate = 48000;

/// Mono 16-bit WAV: a tone for 0.4 s, 0.3 s silence, a tone for 0.3 s (1 s in all).
std::vector<u8> voiceWav()
{
    std::vector<i16> samples(kRate);
    for (u32 i = 0; i < kRate; ++i)
    {
        const f32 t = static_cast<f32>(i) / kRate;
        const bool sound = t < 0.4f || t >= 0.7f;
        samples[i] = sound ? static_cast<i16>(12000.0 * std::sin(6.283185307 * 220.0 * t)) : i16(0);
    }
    std::vector<u8> out;
    const auto put = [&](const void* p, usize n)
    { out.insert(out.end(), static_cast<const u8*>(p), static_cast<const u8*>(p) + n); };
    const auto u32le = [&](u32 v) { put(&v, 4); };
    const auto u16le = [&](u16 v) { put(&v, 2); };
    put("RIFF", 4);
    u32le(36 + kRate * 2);
    put("WAVEfmt ", 8);
    u32le(16);
    u16le(1);
    u16le(1);
    u32le(kRate);
    u32le(kRate * 2);
    u16le(2);
    u16le(16);
    put("data", 4);
    u32le(kRate * 2);
    put(samples.data(), samples.size() * 2);
    return out;
}

/// A folder with voice/de/dia_gate_guard_hello_01.wav, mounted over the game's assets.
struct VoiceFolder
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / std::format("g7_voice_test_{}", std::random_device{}());
    VoiceFolder()
    {
        std::filesystem::create_directories(root / "voice" / "de");
        const std::vector<u8> wav = voiceWav();
        std::ofstream(root / "voice" / "de" / "dia_gate_guard_hello_01.wav", std::ios::binary)
            .write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
    }
    ~VoiceFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

EngineConfig voiceConfig(const VoiceFolder& folder)
{
    EngineConfig config;
    config.appName = "voice";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "12:00";
    config.fixedFrameSeconds = 1.0 / 60.0;
    auto settings = Config::parse(
        std::format("[[assets.mount]]\npath = \"{}\"\npriority = 100\n", folder.root.generic_string()));
    REQUIRE(settings.ok());
    config.settings = std::move(settings).value();
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
    for (int i = 0; i < static_cast<int>(std::lround(seconds * 60.0f)); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

std::string field(Engine& engine, std::string_view name)
{
    return std::string(run(engine, std::format("dialog_state().{}", name)).asString());
}

void startTalk(Engine& engine)
{
    REQUIRE(run(engine, "insert_npc('npc_gate_guard', 'wp_camp_center')").isString());
    run(engine, "set_routine('npc_gate_guard', '') npc_clear('npc_gate_guard')");
    run(engine, "teleport(9, 0, -4)");
    runSeconds(engine, 0.2f);
    REQUIRE(run(engine, "talk('npc_gate_guard')").asBool());
}
} // namespace

TEST_CASE("Engine voice: a line with its take is spoken, the mouth follows the loudness, the music ducks")
{
    VoiceFolder folder;
    Engine engine(voiceConfig(folder));
    REQUIRE(engine.init().ok());
    REQUIRE(engine.audio() != nullptr);
    run(engine, "on('voice_line', function(npc, key, s) Story.voice = npc .. ' ' .. key end)");
    startTalk(engine);
    REQUIRE(engine.runFrame());
    CHECK(field(engine, "key") == "dia_gate_guard_hello_01");
    CHECK(run(engine, "voice_playing('npc_gate_guard')").asBool());
    CHECK(run(engine, "Story.voice").asString() == "npc_gate_guard dia_gate_guard_hello_01");

    // In the sound the mouth opens, in the gap (0.4 - 0.7 s) it closes, then opens again.
    runSeconds(engine, 0.25f);
    CHECK(engine.faceWeight("npc_gate_guard", "vis_aa") > 0.4f);
    CHECK(engine.audio()->busVolume(audio::Bus::Music) == doctest::Approx(0.5f)); // ducked (faded in 0.3 s)
    runSeconds(engine, 0.3f);                                                     // 0.55 s
    CHECK(engine.faceWeight("npc_gate_guard", "vis_aa") < 0.1f);
    runSeconds(engine, 0.25f); // 0.8 s
    CHECK(engine.faceWeight("npc_gate_guard", "vis_aa") > 0.4f);

    // The line lasts as long as the take (1 s) plus the pause (0.3 s): the subtitles follow the voice.
    runSeconds(engine, 0.3f); // 1.1 s
    CHECK(field(engine, "speaker") == "npc_gate_guard");
    runSeconds(engine, 0.35f); // 1.45 s
    CHECK(field(engine, "speaker") == "hero");
    CHECK_FALSE(run(engine, "voice_playing('npc_gate_guard')").asBool());
    CHECK(engine.faceWeight("npc_gate_guard", "vis_aa") < 0.05f);
    // The hero's line has no take: reading time, the rough mouth movement, the music back up.
    CHECK_FALSE(run(engine, "voice_playing('hero')").asBool());
    runSeconds(engine, 0.4f);
    CHECK(engine.audio()->busVolume(audio::Bus::Music) == doctest::Approx(1.0f));
}

TEST_CASE("Engine voice: skipping a spoken line stops its voice")
{
    VoiceFolder folder;
    Engine engine(voiceConfig(folder));
    REQUIRE(engine.init().ok());
    startTalk(engine);
    REQUIRE(engine.runFrame());
    CHECK(run(engine, "voice_playing('npc_gate_guard')").asBool());
    engine.dialogSkip();
    REQUIRE(engine.runFrame());
    CHECK(field(engine, "speaker") == "hero");
    CHECK_FALSE(run(engine, "voice_playing('npc_gate_guard')").asBool());
}

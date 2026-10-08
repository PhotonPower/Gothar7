// Sound in the engine (M13 part A, label "headless"): the mixer without a device, data/sounds.toml, Lua
// sound(), the animation events sound:<name> of the clips (the woodcutter's axe), the volumes of engine.toml
// [audio].

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
EngineConfig audioConfig()
{
    EngineConfig config;
    config.appName = "audio";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "09:00";
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
} // namespace

TEST_CASE(
    "Engine audio: sounds of data/sounds.toml from Lua, in 3D; unknown ones are nil; the volumes of [audio]")
{
    EngineConfig config = audioConfig();
    config.settings.set<f64>("audio.music", 0.5); // as engine.toml [audio] would
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    REQUIRE(engine.audio() != nullptr);
    CHECK(engine.audio()->busVolume(audio::Bus::Music) == doctest::Approx(0.5f));
    CHECK(engine.audio()->busVolume(audio::Bus::Effects) == doctest::Approx(1.0f));
    const auto id = run(engine, "return sound('anvil_hit', 40, 1, 18)");
    REQUIRE(id.isNumber());
    CHECK(run(engine, std::format("return sound_playing({})", id.asInteger())).asBool());
    runSeconds(engine, 0.2f);
    CHECK(engine.audio()->lastPeak() > 0.01f); // heard (the hero stands near)
    runSeconds(engine, 1.5f);
    CHECK_FALSE(run(engine, std::format("return sound_playing({})", id.asInteger())).asBool()); // played out
    CHECK(run(engine, "return sound('no_such_sound')").isNil());
    CHECK(engine.soundsPlayed("anvil_hit") == 1);
}

TEST_CASE("Engine audio: the woodcutter's axe sounds at its clip's event (sound:wood_chop)")
{
    Engine engine(audioConfig());
    REQUIRE(engine.init().ok());
    REQUIRE(run(engine, "return insert_npc('npc_woodcutter')").isString()); // 07-18: chopping wood
    runSeconds(engine, 30.0f);
    CHECK(engine.soundsPlayed("wood_chop") > 0);
}

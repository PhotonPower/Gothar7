// Sound in the engine (M13 part A, label "headless"): the mixer without a device, data/sounds.toml, Lua
// sound(), the animation events sound:<name> of the clips (the woodcutter's axe), the volumes of engine.toml
// [audio].

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
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

TEST_CASE(
    "Engine audio: the ambience of the zone - the camp in the forest (the smallest box), by night another "
    "loop; single sounds around the listener (M13 part B)")
{
    Engine engine(audioConfig());
    REQUIRE(engine.init().ok());
    run(engine, "time(12, 0) teleport(8, 0, 4)"); // in the camp
    runSeconds(engine, 0.5f);
    CHECK(engine.ambience() == "camp");
    CHECK(engine.soundsPlayed("amb_camp") == 1);
    run(engine, "teleport(60, 0, 0)"); // outside the fence: the forest around it
    runSeconds(engine, 0.5f);
    CHECK(engine.ambience() == "wald");
    CHECK(engine.soundsPlayed("amb_wind") == 1);
    runSeconds(engine, 40.0f); // birds now and then (every 6-16 s)
    CHECK(engine.soundsPlayed("bird") >= 2);
    run(engine, "time(23, 0)");
    runSeconds(engine, 0.5f);
    CHECK(engine.soundsPlayed("amb_night") == 1); // the night's loop
}

TEST_CASE(
    "Engine audio: a wall between the camera and a sound muffles it, in the open it is clear (M13 part B)")
{
    Engine engine(audioConfig());
    REQUIRE(engine.init().ok());
    run(engine, "time(12, 0)");
    runSeconds(engine, 0.5f);
    const Vec3 ear = engine.camera().transform.position;
    const auto blocked = [&](const Vec3& p)
    {
        const Vec3 to = p - ear;
        return engine.physics()
            .raycast(ear, glm::normalize(to), glm::length(to) - 0.3f,
                     physics::layerBit(physics::Layer::World))
            .has_value();
    };
    // Somewhere around the camera behind something solid (the camp's houses, fences, trees), and one in the
    // open.
    std::optional<Vec3> hidden;
    std::optional<Vec3> seen;
    for (f32 r = 6.0f; r < 40.0f && (!hidden || !seen); r += 2.0f)
    {
        for (f32 a = 0.0f; a < 6.28f && (!hidden || !seen); a += 0.2f)
        {
            const Vec3 p(ear.x + std::cos(a) * r, 1.5f, ear.z + std::sin(a) * r);
            if (blocked(p))
            {
                hidden = hidden.value_or(p);
            }
            else
            {
                seen = seen.value_or(p);
            }
        }
    }
    REQUIRE(hidden.has_value());
    REQUIRE(seen.has_value());
    const Vec3 inside = *hidden;
    const Vec3 open = *seen;
    const auto walled =
        run(engine, std::format("return sound('anvil_hit', {}, {}, {})", inside.x, inside.y, inside.z));
    const auto clear =
        run(engine, std::format("return sound('anvil_hit', {}, {}, {})", open.x, open.y, open.z));
    REQUIRE(walled.isNumber());
    REQUIRE(clear.isNumber());
    runSeconds(engine, 0.3f);
    CHECK(engine.soundMuffle(static_cast<audio::SoundId>(walled.asInteger())) > 0.5f);
    CHECK(engine.soundMuffle(static_cast<audio::SoundId>(clear.asInteger())) < 0.01f);
}

TEST_CASE("Engine audio: in a room the ambience innen, the outside one quiet and muffled behind it; a "
          "smaller ambient "
          "box in the room wins (the forge)")
{
    // A floor, the forest around, a room (indoor zone) and in its corner the forge (an ambient box smaller
    // than the room).
    const std::filesystem::path world = std::filesystem::temp_directory_path() / "g7_audio_rooms.g7world";
    {
        std::ofstream out(world, std::ios::binary);
        out << R"({"version":1,"name":"rooms","nextVobId":3,"vobs":[)"
            << R"({"id":1,"type":"mesh","name":"FLOOR","pos":[0.0,-0.75,0.0],"rot":[0.0,0.0,0.0,1.0],)"
            << R"("scale":[200.0,1.0,200.0],"mesh":"mobs/table.glb"},)"
            << R"({"id":2,"type":"start","name":"START","pos":[30.0,0.1,0.0],"rot":[0.0,0.0,0.0,1.0]}],)"
            << R"("zones":[)"
            << R"({"type":"ambient","value":"wald","box":{"center":[0.0,5.0,0.0],"halfExtents":[100.0,50.0,100.0],"yaw":0.0}},)"
            << R"({"type":"indoor","value":"ROOM","box":{"center":[0.0,1.5,0.0],"halfExtents":[4.0,1.5,4.0],"yaw":0.0}},)"
            << R"({"type":"ambient","value":"schmiede_esse","box":{"center":[3.0,1.5,3.0],"halfExtents":[1.0,1.5,1.0],"yaw":0.0}}]})";
    }
    EngineConfig config = audioConfig();
    config.world = world;
    config.start = "START";
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    run(engine, "time(12, 0)");
    runSeconds(engine, 0.5f);
    CHECK(engine.ambience() == "wald"); // outside
    CHECK(engine.ambienceOutside().empty());
    run(engine, "teleport(-2, 0.1, -2)"); // in the room
    runSeconds(engine, 0.5f);
    CHECK(engine.ambience() == "innen");
    CHECK(engine.ambienceOutside() == "wald"); // the forest behind the walls
    CHECK(engine.soundsPlayed("amb_room") == 1);
    CHECK(engine.soundsPlayed("amb_wind") == 2); // once outside, once muffled from inside
    run(engine, "teleport(3, 0.1, 3)");          // at the forge
    runSeconds(engine, 0.5f);
    CHECK(engine.ambience() == "schmiede_esse");
    CHECK(engine.ambienceOutside().empty());
    std::filesystem::remove(world);
}

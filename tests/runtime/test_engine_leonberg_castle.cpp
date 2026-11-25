// The residents of the castle (owner decision F1, welt #447; label "headless", with the generated town - not
// in CI: generated files are not versioned): the Vogt, two guards, the cook and the scribe follow their
// routines in the castle's rooms. Its own file and ctest entry for the run time.

#include <g7/ai/Waynet.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
const std::string kGenerated =
    std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/generated/leonberg_terrain.r16";

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

/// Leonberg at `time`, the hero in the castle's hall (near by: simulated), `seconds` of play.
void playCastle(Engine& engine, f32 seconds)
{
    const auto hall = engine.waynet().find("WP_LEO_SCHLOSS_HALLE");
    REQUIRE(hall.has_value());
    const Vec3 p = engine.waynet().points()[*hall].position;
    run(engine, std::format("teleport({}, {}, {})", p.x, p.y + 0.1f, p.z));
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

EngineConfig castleConfig(const char* time)
{
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.startTime = time;
    config.fixedFrameSeconds = 1.0 / 60.0;
    return config;
}

/// Metres (on the ground plane) from the NPC to a way point or freepoint.
f32 distanceTo(Engine& engine, std::string_view npc, std::string_view place)
{
    const auto wp = engine.waynet().find(place);
    const auto fp = engine.waynet().findFreepoint(place);
    REQUIRE_MESSAGE((wp || fp), place);
    const Vec3 w = wp ? engine.waynet().points()[*wp].position : engine.waynet().freepoints()[*fp].position;
    const f32 x = static_cast<f32>(run(engine, std::format("npc_state('{}').x", npc)).asNumber());
    const f32 z = static_cast<f32>(run(engine, std::format("npc_state('{}').z", npc)).asNumber());
    return glm::length(Vec2(x - w.x, z - w.z));
}
} // namespace

TEST_CASE(
    "Leonberg castle: by night the Vogt, the cook, the scribe and the day guard sleep in their beds, the "
    "night guard watches the hall (with the generated town)")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    Engine engine(castleConfig("23:00"));
    REQUIRE(engine.init().ok());
    for (const char* npc : {"npc_leo_vogt", "npc_leo_castle_guard_day", "npc_leo_castle_guard_night",
                            "npc_leo_castle_cook", "npc_leo_castle_scribe"})
    {
        CHECK_MESSAGE(
            run(engine, std::format("npc_state('{}').routine", npc)).asString().starts_with("rtn_leo_"), npc);
    }
    playCastle(engine, 30.0f);
    for (const char* npc :
         {"npc_leo_vogt", "npc_leo_castle_guard_day", "npc_leo_castle_cook", "npc_leo_castle_scribe"})
    {
        CHECK_MESSAGE(run(engine, std::format("npc_state('{}').mob", npc)).asString() == "bed", npc);
    }
    // The chamber is large: its bed lies about 9 m from the way point - on the upper floor, not in the guard
    // room.
    CHECK(distanceTo(engine, "npc_leo_vogt", "WP_LEO_SCHLOSS_GEMACH") < 10.0f);
    const auto chamber = engine.waynet().find("WP_LEO_SCHLOSS_GEMACH");
    REQUIRE(chamber.has_value());
    CHECK(std::abs(static_cast<f32>(run(engine, "npc_state('npc_leo_vogt').y").asNumber()) -
                   engine.waynet().points()[*chamber].position.y) < 1.5f);
    CHECK(distanceTo(engine, "npc_leo_castle_cook", "WP_LEO_SCHLOSS_GESINDE") < 7.0f);
    CHECK(run(engine, "npc_state('npc_leo_castle_guard_night').state").asString() == "zs_stand_guarding");
    CHECK(distanceTo(engine, "npc_leo_castle_guard_night", "FP_STAND_LEO_SCHLOSS_HALLE_01") < 1.5f);
}

TEST_CASE("Leonberg castle: in the morning the scribe sits in the writing room, the cook stirs at the hearth "
          "(with the generated town)")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    Engine engine(castleConfig("09:30"));
    REQUIRE(engine.init().ok());
    playCastle(engine, 30.0f);
    CHECK(run(engine, "npc_state('npc_leo_castle_scribe').mob").asString() == "table"); // zs_sit_table
    CHECK(distanceTo(engine, "npc_leo_castle_scribe", "WP_LEO_SCHLOSS_SCHREIBSTUBE") < 6.0f);
    CHECK(distanceTo(engine, "npc_leo_castle_cook", "FP_COOK_LEO_SCHLOSS_KUECHE_01") < 1.5f);
    CHECK(run(engine, "npc_state('npc_leo_castle_cook').state").asString() == "zs_cook");
    MESSAGE("cook: ", std::string(run(engine, "npc_state('npc_leo_castle_cook').animation").asString()));
    CHECK(run(engine, "npc_state('npc_leo_castle_cook').animation").asString().starts_with("amb_cook"));
    CHECK(distanceTo(engine, "npc_leo_castle_guard_day", "FP_STAND_LEO_SCHLOSS_HALLE_01") < 1.5f);
    CHECK(engine.scripts()->callErrors() == 0);
}

TEST_CASE("Leonberg castle: in the afternoon the scribe reads at a lectern in the library (with the "
          "generated town)")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    Engine engine(castleConfig("14:30"));
    REQUIRE(engine.init().ok());
    playCastle(engine, 30.0f);
    CHECK(run(engine, "npc_state('npc_leo_castle_scribe').state").asString() == "zs_read");
    MESSAGE("scribe: ", std::string(run(engine, "npc_state('npc_leo_castle_scribe').animation").asString()));
    CHECK(run(engine, "npc_state('npc_leo_castle_scribe').animation").asString().starts_with("amb_read"));
    CHECK(std::min(distanceTo(engine, "npc_leo_castle_scribe", "FP_READ_LEO_SCHLOSS_BIBLIOTHEK_01"),
                   distanceTo(engine, "npc_leo_castle_scribe", "FP_READ_LEO_SCHLOSS_BIBLIOTHEK_02")) < 1.5f);
    CHECK(engine.scripts()->callErrors() == 0);
}

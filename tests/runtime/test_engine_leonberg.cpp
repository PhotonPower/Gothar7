// Leonberg lebt (label "headless"): the residents' routines name places of welt's waynet (checked against the
// versioned world file, also in CI); and, where the generated town is there (locally, not in CI: generated
// files are not versioned), a game day in Leonberg with each resident in the state of his routine at the
// right place.

#include <g7/ai/Waynet.hpp>
#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/WorldFile.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <ostream> // doctest needs it to print std::string operands
#include <sstream>
#include <string>

using namespace g7;

namespace
{
const std::string kWorld = std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/leonberg.g7world";
const std::string kGenerated =
    std::string(G7_ASSET_SOURCE_DIR) + "/worlds/leonberg/generated/leonberg_terrain.r16";

constexpr std::array<const char*, 8> kResidents = {
    "npc_leo_smith",       "npc_leo_innkeeper",   "npc_leo_baker",  "npc_leo_market",
    "npc_leo_guard_lower", "npc_leo_guard_upper", "npc_leo_farmer", "npc_leo_citizen"};

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}
} // namespace

TEST_CASE("Leonberg residents: every routine place is in Leonberg's waynet, every state exists")
{
    std::ifstream in(kWorld, std::ios::binary);
    REQUIRE(in.good());
    std::stringstream text;
    text << in.rdbuf();
    auto world = world::parseWorldFile(text.str(), "leonberg.g7world");
    REQUIRE_MESSAGE(world.ok(), (world.ok() ? "" : world.error().message));
    REQUIRE(world.value().waynet.has_value());
    const ai::Waynet waynet = ai::Waynet::build(*world.value().waynet);

    // The scripts (any world: Leonberg's geometry is not needed for this).
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    usize entries = 0;
    for (const char* npc : kResidents)
    {
        const script::Instance* def = engine.scripts()->findInstance("Npc", npc);
        REQUIRE_MESSAGE(def != nullptr, npc);
        const script::Instance* routine =
            engine.scripts()->findInstance("Routine", def->fields["routine"].asString());
        REQUIRE_MESSAGE(routine != nullptr, npc);
        for (const script::Value& e : routine->fields.asTable()->array)
        {
            const std::string at(e["at"].asString());
            CHECK_MESSAGE((waynet.find(at) || waynet.findFreepoint(at)), routine->name, ": ", at);
            CHECK_MESSAGE(engine.scripts()->findInstance("State", e["state"].asString()) != nullptr,
                          routine->name, ": ", e["state"].asString());
            ++entries;
        }
    }
    CHECK(entries >= 24);
}

TEST_CASE(
    "Leonberg residents: a game day in the town, each at the place of his routine (with the generated town)")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here (not versioned; welt's gothar-worldgen "
                "makes them)");
        return;
    }
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.startTime = "06:30";
    config.fixedFrameSeconds = 1.0 / 60.0;
    auto settings = Config::parse("[time]\nminute_seconds = 0.25\n");
    REQUIRE(settings.ok());
    config.settings = std::move(settings).value();
    Engine engine(std::move(config));
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    // Placed by themselves when Leonberg loaded (owner decision L2).
    for (const char* npc : kResidents)
    {
        CHECK_MESSAGE(
            run(engine, std::format("npc_state('{}').routine", npc)).asString().starts_with("rtn_leo_"), npc);
    }
    const u64 errorsBefore = engine.scripts()->callErrors();
    // The player far outside the town: the residents jump to their routines' places (AI LOD).
    run(engine, "teleport(1500, 0, 1500)");
    for (int hour = 0; hour < 24; ++hour)
    {
        for (int i = 0; i < 15 * 60; ++i) // 60 game minutes
        {
            REQUIRE(engine.runFrame());
        }
        for (const char* npc : kResidents)
        {
            const std::string at(run(engine, std::format("npc_state('{}').at", npc)).asString());
            const auto wp = engine.waynet().find(at);
            const auto fp = engine.waynet().findFreepoint(at);
            REQUIRE_MESSAGE((wp || fp), npc, " at ", at);
            const Vec3 place =
                wp ? engine.waynet().points()[*wp].position : engine.waynet().freepoints()[*fp].position;
            const f32 x = static_cast<f32>(run(engine, std::format("npc_state('{}').x", npc)).asNumber());
            const f32 z = static_cast<f32>(run(engine, std::format("npc_state('{}').z", npc)).asNumber());
            CHECK_MESSAGE(glm::length(Vec2(x - place.x, z - place.z)) < 1.0f, npc, " at ", at);
        }
    }
    CHECK(engine.scripts()->callErrors() == errorsBefore);
}

TEST_CASE(
    "Leonberg: the camera's indoor profile in the inn, the outside one on the market (with the generated "
    "town)")
{
    if (!std::filesystem::exists(kGenerated))
    {
        MESSAGE("skipped: Leonberg's generated files are not here");
        return;
    }
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.fixedFrameSeconds = 1.0 / 60.0;
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    for (int i = 0; i < 120; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.playerIndoorBlend() < 0.05f); // the market square
    const auto inn = engine.waynet().find("WP_LEO_GASTHAUS_ZHE_INNEN");
    REQUIRE(inn.has_value());
    const Vec3 p = engine.waynet().points()[*inn].position;
    run(engine, std::format("teleport({}, {}, {})", p.x, p.y + 0.1f, p.z));
    for (int i = 0; i < 180; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    CHECK(engine.playerIndoorBlend() > 0.95f);
    CHECK(engine.playerCameraDistance() <= 1.95f);
}

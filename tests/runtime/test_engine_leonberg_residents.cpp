// Leonberg lebt (label "headless"; its own ctest entry for its run time): the residents' routines name places
// of welt's waynet (checked against the versioned world file, also in CI); and, where the generated town is
// there (locally, not in CI: generated files are not versioned), a game day in Leonberg with each resident in
// the state of his routine at the right place.

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

constexpr std::array<const char*, 19> kResidents = {
    "npc_leo_smith", "npc_leo_innkeeper", "npc_leo_baker", "npc_leo_market", "npc_leo_guard_lower",
    "npc_leo_guard_upper", "npc_leo_farmer", "npc_leo_citizen",
    // welt phase 1: the craftsmen and merchants
    "npc_leo_baker_husband", "npc_leo_butcher", "npc_leo_joiner", "npc_leo_potter", "npc_leo_goldsmith",
    "npc_leo_cloth_merchant", "npc_leo_tailor", "npc_leo_herbalist", "npc_leo_bather", "npc_leo_merchant_m",
    "npc_leo_merchant_f"};

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
    CHECK(entries >= 70);
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

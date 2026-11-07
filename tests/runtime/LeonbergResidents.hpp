#pragma once

// Leonberg's residents for the runtime tests (test_engine_leonberg_residents*.cpp): the 25 of them and a part
// of a game day in the town, each checked at the place of his routine every game hour.

#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <string_view>

namespace g7::test
{
inline constexpr std::array<const char*, 25> kResidents = {
    "npc_leo_smith", "npc_leo_innkeeper", "npc_leo_baker", "npc_leo_market", "npc_leo_guard_lower",
    "npc_leo_guard_upper", "npc_leo_farmer", "npc_leo_citizen",
    // welt phase 1: the craftsmen and merchants
    "npc_leo_baker_husband", "npc_leo_butcher", "npc_leo_joiner", "npc_leo_potter", "npc_leo_goldsmith",
    "npc_leo_cloth_merchant", "npc_leo_tailor", "npc_leo_herbalist", "npc_leo_bather", "npc_leo_merchant_m",
    "npc_leo_merchant_f",
    // church, beggars and music
    "npc_leo_priest", "npc_leo_monk_1", "npc_leo_monk_2", "npc_leo_beggar_m", "npc_leo_beggar_f",
    "npc_leo_musician"};

inline script::Value runLine(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

/// `hours` game hours in Leonberg from `start` ("06:30"), the player far outside the town (AI LOD: the
/// residents jump to their places); after each hour every resident must be at the place of his routine's
/// state.
inline void playLeonbergHours(std::string_view start, u32 hours)
{
    const auto run = [](Engine& engine, std::string_view line) { return runLine(engine, line); };
    EngineConfig config;
    config.appName = "leonberg";
    config.headless = true;
    config.world = fs::fromUtf8("worlds/leonberg/leonberg.g7world");
    config.start = "START_MARKTPLATZ";
    config.startTime = std::string(start);
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
    for (u32 hour = 0; hour < hours; ++hour)
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
} // namespace g7::test

#pragma once

// Perf scenarios (docs/modules/perf.md part B): data/perf.toml names fixed views in Leonberg, the times of
// day to measure them at, and optionally extra NPCs (stress). gothar --perf=<name> runs them.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace g7::perf
{
struct Scenario
{
    std::string name;
    std::string description;
    Vec3 camera{0.0f}; ///< free camera (like --cam), metres
    f32 yawDegrees = 0.0f;
    f32 pitchDegrees = 0.0f;
    Vec3 player{0.0f};             ///< the hero's feet at a fixed spot (default: 1.6 m below the camera)
    std::vector<u32> minutesOfDay; ///< one run per time, e.g. 600 for 10:00
    u32 extraNpcs = 0;             ///< stress: this many NPCs more, on way points around the hero
    std::vector<std::string> npcs; ///< their Npc instances, in turn
    f32 npcRadius = 25.0f;         ///< way points within this distance of the hero
};

struct PerfFile
{
    f64 warmupSeconds = 5.0; ///< per run, before measuring (at least until no model load is pending)
    u32 frames = 600;        ///< measured frames per run
    std::vector<Scenario> scenarios;
};

/// "21:00" -> 1260; errors for anything else.
[[nodiscard]] Result<u32> parseClock(std::string_view text);
/// Reads perf.toml; `source` names it in errors.
[[nodiscard]] Result<PerfFile> parsePerfFile(std::string_view toml, std::string_view source);
/// "all" (every scenario, the stress ones last), or names separated by commas, in that order.
[[nodiscard]] Result<std::vector<Scenario>> selectScenarios(const PerfFile& file, std::string_view names);
} // namespace g7::perf

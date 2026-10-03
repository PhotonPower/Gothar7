#pragma once

// Routes for the autopilot (gothar --walk, docs/modules/tools.md "Autopilot"): waypoints the player runs
// to, with actions, screenshots and teleports. Format version 1, agreed with welt (W3 walkthrough).

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::walk
{
enum class Gait : u8
{
    Run,
    Walk,
    Sneak,
};

struct RoutePoint
{
    enum class Action : u8
    {
        None,
        Jump,  ///< the jump key once, 2 m before the point
        Climb, ///< the jump key in front of a ledge (climbs if one is in reach)
    };
    std::string name;
    Vec2 pos{0.0f};       ///< x, z in metres
    std::optional<f32> y; ///< teleport height (feet); without it the highest ground below
    f32 radius = 1.0f;    ///< reached within this horizontal distance
    std::optional<Gait> gait;
    Action action = Action::None;
    bool screenshot = false;
    bool teleport = false; ///< put there instead of running
};

struct Route
{
    std::string start; ///< start point (optional)
    Gait gait = Gait::Run;
    std::string time;           ///< game time "HH:MM" (optional)
    f64 timeLimit = 900.0;      ///< seconds of game time for the whole route
    bool screenshotAll = false; ///< "screenshot": "all"
    f32 screenshotEveryM = 0.0f;
    std::vector<RoutePoint> points;
};

/// route.json, version 1. Errors name the source and the point.
[[nodiscard]] Result<Route> parseRoute(std::string_view json, std::string_view source);
[[nodiscard]] std::string_view gaitName(Gait gait) noexcept;
} // namespace g7::walk

#pragma once

// Checks of a waynet (M16 part B): the recommendations of world.md "Wegnetz" as a list the editor shows with
// a jump to each place. The world itself (walls, ground) comes through callbacks, so the checks run without
// physics.

#include <g7/core/Math.hpp>
#include <g7/world/Waynet.hpp>

#include <functional>
#include <string>
#include <vector>

namespace g7::editor
{
struct WaynetIssue
{
    enum class Kind : u8
    {
        Unconnected,   ///< a way point without edges
        Island,        ///< a way point outside the largest connected part
        ShortEdge,     ///< below minEdge
        LongEdge,      ///< above maxEdge
        Blocked,       ///< something solid on the straight line at hip height
        NotOnGround,   ///< no walkable ground under the point
        LoneFreepoint, ///< no way point within freepointReach
    };
    Kind kind = Kind::Unconnected;
    std::string name;    ///< the point or freepoint; for edges the first end
    std::string other;   ///< edges: the second end
    Vec3 position{0.0f}; ///< where to look (edges: their middle)
    std::string text;    ///< for the list
};

struct WaynetCheckSettings
{
    f32 minEdge = 5.0f; ///< world.md: connected points 5-20 m apart
    f32 maxEdge = 20.0f;
    f32 hipHeight = 0.9f;      ///< the straight line between two points is free at this height
    f32 freepointReach = 3.0f; ///< a freepoint has a way point this near
};

/// Something solid between the two points (world space)?
using BlockedFn = std::function<bool(const Vec3& from, const Vec3& to)>;
/// Walkable ground at the point (its feet)?
using GroundFn = std::function<bool(const Vec3& feet)>;

/// The issues of `waynet`, sorted by kind, then name. `blocked`/`ground` may be empty (those checks are left
/// out).
[[nodiscard]] std::vector<WaynetIssue> checkWaynet(const world::WaynetData& waynet,
                                                   const WaynetCheckSettings& settings,
                                                   const BlockedFn& blocked, const GroundFn& ground);
} // namespace g7::editor

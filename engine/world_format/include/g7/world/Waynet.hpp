#pragma once

// The waynet of a world as stored in .g7world (block "waynet", v1, contract with welt - docs/modules/world.md
// "Wegnetz"): way points (WP_), freepoints (FP_<TYPE>_) and undirected edges by name. Path finding uses it
// with M9.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::world
{
struct WaynetPoint
{
    std::string name;
    Vec3 position{0.0f};     ///< metres, world space; y = ground (feet)
    std::optional<Vec3> dir; ///< horizontal facing for whoever stands there (normalised)
    bool generated = false;  ///< owner "worldgen": the generator replaces it on its next run
};

struct WaynetEdge
{
    std::string a; ///< the smaller name first (as written)
    std::string b;
    bool generated = false;
};

struct WaynetData
{
    std::vector<WaynetPoint> points;
    std::vector<WaynetEdge> edges;
    std::vector<WaynetPoint> freepoints;

    [[nodiscard]] bool empty() const noexcept
    {
        return points.empty() && edges.empty() && freepoints.empty();
    }
    [[nodiscard]] const WaynetPoint* findPoint(std::string_view name) const noexcept;
    [[nodiscard]] const WaynetPoint* findFreepoint(std::string_view name) const noexcept;
};

/// "FP_SIT_LEO_BRUNNEN_01" -> "SIT"; empty for names that are no freepoint.
[[nodiscard]] std::string_view freepointType(std::string_view name) noexcept;
/// Upper-case letters, digits and '_', starting with `prefix` ("WP_" or "FP_") and something after it.
[[nodiscard]] bool validWaynetName(std::string_view name, std::string_view prefix) noexcept;
/// Checks names (valid, unique over points and freepoints), edges (both ends existing points, not to itself);
/// sorts points and freepoints by name and edges by their names, merges duplicate edges. `where` prefixes
/// errors ("camp.g7world: waynet.edges[2]: unknown point \"WP_X\"").
[[nodiscard]] Result<void> normalizeWaynet(WaynetData& waynet, std::string_view where);
} // namespace g7::world

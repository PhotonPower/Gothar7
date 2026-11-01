#pragma once

// The camera flight behind the main menu (M14 part F, F8): points as data (ui/menu_flight.toml), a smooth
// Catmull-Rom path through the positions and one through the points looked at, at a steady speed.

#include <g7/core/Config.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <vector>

namespace g7
{
struct MenuFlightPoint
{
    Vec3 position{0.0f}; ///< m
    Vec3 look{0.0f};     ///< the point looked at (m)
};

struct MenuFlightPose
{
    Vec3 position{0.0f};
    Vec3 look{0.0f};
};

class MenuFlight
{
public:
    /// `version = 1`, `speed` (m/s, default 3), `loop` (default true), `[[point]] position = [x, y, z]
    /// look = [x, y, z]`; at least two points.
    [[nodiscard]] static Result<MenuFlight> fromConfig(const Config& config);
    MenuFlight() = default;
    MenuFlight(std::vector<MenuFlightPoint> points, f32 speed, bool loop);

    [[nodiscard]] bool empty() const noexcept { return m_points.size() < 2; }
    /// Seconds for one pass (back at the start when looping).
    [[nodiscard]] f32 duration() const noexcept { return m_total; }
    /// The pose `seconds` after the start; looping wraps, otherwise it stays at the last point.
    [[nodiscard]] MenuFlightPose at(f32 seconds) const;

private:
    std::vector<MenuFlightPoint> m_points;
    std::vector<f32> m_segmentSeconds; ///< from point i to i + 1 (and last to first when looping)
    f32 m_total = 0.0f;
    bool m_loop = true;
};
} // namespace g7

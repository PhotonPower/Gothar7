#pragma once

// Water (M5 part E): the water vobs of a scene as level boxes, asked where the surface is.
// Contract: world.md "Vob-Typen" (water); swimming: gameplay.md.

#include <g7/core/Math.hpp>
#include <g7/world/Components.hpp>

#include <optional>
#include <span>
#include <vector>

namespace g7::world
{
class Scene;

/// One water box in world space.
struct WaterBody
{
    VobId vob;
    Vec3 centre{0.0f};
    f32 yaw = 0.0f; ///< radians about +Y
    Vec3 halfExtents{1.0f};
    [[nodiscard]] f32 surface() const noexcept { return centre.y + halfExtents.y; }
    [[nodiscard]] f32 bottom() const noexcept { return centre.y - halfExtents.y; }
    /// True if x, z lies inside the box's footprint.
    [[nodiscard]] bool covers(f32 x, f32 z) const noexcept;
};

class WaterBodies
{
public:
    /// From the WaterVolume vobs of `scene` (its world transforms must be up to date).
    void rebuild(const Scene& scene);
    void clear() noexcept { m_bodies.clear(); }

    /// Water surface over `point`: the highest top among the boxes whose footprint contains it and that
    /// reach from below it to at most `above` metres under it (a bridge high over a river is dry).
    /// nullopt: no water there.
    [[nodiscard]] std::optional<f32> surfaceAt(const Vec3& point, f32 above = 0.5f) const noexcept;
    [[nodiscard]] std::span<const WaterBody> bodies() const noexcept { return m_bodies; }

private:
    std::vector<WaterBody> m_bodies;
};
} // namespace g7::world

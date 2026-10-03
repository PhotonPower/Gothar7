#include <g7/world/Scene.hpp>
#include <g7/world/Water.hpp>

#include <cmath>

namespace g7::world
{
bool WaterBody::covers(f32 x, f32 z) const noexcept
{
    // Into the box's frame: turn the offset back by its yaw.
    const f32 dx = x - centre.x;
    const f32 dz = z - centre.z;
    const f32 c = std::cos(yaw);
    const f32 s = std::sin(yaw);
    const f32 localX = c * dx - s * dz;
    const f32 localZ = s * dx + c * dz;
    return std::abs(localX) <= halfExtents.x && std::abs(localZ) <= halfExtents.z;
}

void WaterBodies::rebuild(const Scene& scene)
{
    m_bodies.clear();
    scene.each<Vob, WaterVolume, WorldTransform>(
        [&](entt::entity, const Vob& vob, const WaterVolume& water, const WorldTransform& world)
        {
            const Vec3 forward = Mat3(world.matrix) * Vec3(0.0f, 0.0f, -1.0f);
            m_bodies.push_back(
                {vob.id, Vec3(world.matrix[3]), std::atan2(-forward.x, -forward.z), water.halfExtents});
        });
}

std::optional<f32> WaterBodies::surfaceAt(const Vec3& point, f32 above) const noexcept
{
    std::optional<f32> best;
    for (const WaterBody& body : m_bodies)
    {
        if (point.y >= body.bottom() && point.y <= body.surface() + above && body.covers(point.x, point.z) &&
            (!best || body.surface() > *best))
        {
            best = body.surface();
        }
    }
    return best;
}
} // namespace g7::world

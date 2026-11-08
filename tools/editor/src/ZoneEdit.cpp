#include <g7/editor/Operations.hpp>
#include <g7/editor/ZoneEdit.hpp>

#include <algorithm>
#include <format>

namespace g7::editor
{
bool editableZone(const world::Zone& zone) noexcept
{
    return zone.box.has_value() &&
           std::ranges::find(kZoneTypes, std::string_view(zone.type)) != std::end(kZoneTypes);
}

Transform zoneTransform(const world::ZoneBox& box) noexcept
{
    Transform t;
    t.position = box.center;
    t.rotation = glm::angleAxis(glm::radians(box.yawDegrees), Vec3(0.0f, 1.0f, 0.0f));
    t.scale = box.halfExtents;
    return t;
}

world::ZoneBox zoneBoxFrom(const Transform& transform) noexcept
{
    // The yaw of the turned +X: (cos yaw, 0, -sin yaw).
    const Vec3 x = transform.rotation * Vec3(1.0f, 0.0f, 0.0f);
    f32 yaw = glm::degrees(std::atan2(-x.z, x.x));
    yaw = std::abs(yaw) < 1e-4f ? 0.0f : yaw;
    return world::ZoneBox{transform.position, glm::max(transform.scale, Vec3(0.05f)), yaw};
}

Mat4 zoneMatrix(const world::ZoneBox& box) noexcept
{
    return glm::translate(Mat4(1.0f), box.center) *
           glm::rotate(Mat4(1.0f), glm::radians(box.yawDegrees), Vec3(0.0f, 1.0f, 0.0f));
}

std::optional<usize> smallestZoneIndex(std::span<const world::Zone> zones, std::string_view type,
                                       const Vec3& point) noexcept
{
    const world::Zone* z = world::smallestZone(zones, type, point);
    return z != nullptr ? std::optional<usize>(static_cast<usize>(z - zones.data())) : std::nullopt;
}

std::optional<usize> pickZone(std::span<const world::Zone> zones, const Ray& ray) noexcept
{
    std::optional<usize> nearest;
    f32 nearestT = 0.0f;
    std::optional<usize> smallestAround;
    f32 smallestVolume = 0.0f;
    for (usize i = 0; i < zones.size(); ++i)
    {
        const world::Zone& z = zones[i];
        if (!z.box)
        {
            continue;
        }
        const Mat4 inverse = glm::inverse(zoneMatrix(*z.box));
        const Ray local{Vec3(inverse * Vec4(ray.origin, 1.0f)), Vec3(inverse * Vec4(ray.direction, 0.0f))};
        const AABB box{-z.box->halfExtents, z.box->halfExtents};
        if (!intersect(local, box))
        {
            continue;
        }
        if (world::zoneContains(*z.box, ray.origin))
        {
            const f32 volume = z.box->halfExtents.x * z.box->halfExtents.y * z.box->halfExtents.z;
            if (!smallestAround || volume < smallestVolume)
            {
                smallestAround = i;
                smallestVolume = volume;
            }
            continue;
        }
        const f32 t = *intersect(local, box);
        if (!nearest || t < nearestT)
        {
            nearest = i;
            nearestT = t;
        }
    }
    return nearest ? nearest : smallestAround;
}

std::vector<std::string> zoneValues(std::string_view type, std::span<const world::Zone> zones,
                                    std::span<const std::string> musicThemes,
                                    std::span<const std::string> ambiences)
{
    std::vector<std::string> values;
    if (type == "music")
    {
        for (const std::string& t : musicThemes)
        {
            if (t != "common")
            {
                values.push_back(t);
            }
        }
    }
    else if (type == "ambient")
    {
        values.assign(ambiences.begin(), ambiences.end());
    }
    else
    {
        for (const world::Zone& z : zones)
        {
            if (z.type == type && !z.value.empty())
            {
                values.push_back(z.value);
            }
        }
    }
    std::ranges::sort(values);
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

world::Zone newZone(std::string type, std::string value, const Vec3& centre)
{
    world::Zone z;
    const Vec3 half = type == "indoor" ? Vec3(3.0f, 1.5f, 3.0f) : Vec3(20.0f, 10.0f, 20.0f);
    z.type = std::move(type);
    z.value = std::move(value);
    z.box = world::ZoneBox{centre, half, 0.0f};
    return z;
}

std::string zoneLabel(const world::Zone& zone)
{
    if (!zone.box)
    {
        return std::format("{} {} (no box, kept)", zone.type, zone.value);
    }
    const Vec3 size = zone.box->halfExtents * 2.0f;
    return std::format("{} {} ({:.4g} x {:.4g} x {:.4g} m)", zone.type, zone.value, size.x, size.y, size.z);
}
} // namespace g7::editor

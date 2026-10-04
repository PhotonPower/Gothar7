#include <g7/render/Visibility.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace g7::render
{
f32 distanceTo(const AABB& box, const Vec3& point) noexcept
{
    return glm::length(glm::clamp(point, box.min, box.max) - point);
}

u32 selectLod(f32 distance, u32 current, const LodSettings& settings) noexcept
{
    if (settings.forced >= 0)
    {
        return static_cast<u32>(settings.forced);
    }
    const f32 thresholds[] = {settings.lod1Distance, settings.lod2Distance}; // level n+1 from thresholds[n]
    u32 level = std::min<u32>(current, 2);
    while (level < 2 && distance > thresholds[level] * (1.0f + settings.hysteresis))
    {
        ++level;
    }
    while (level > 0 && distance < thresholds[level - 1] * (1.0f - settings.hysteresis))
    {
        --level;
    }
    return level;
}

CullResult cullByDistance(const AABB& bounds, const Vec3& eye, const CullSettings& settings,
                          bool sizeCullable) noexcept
{
    const f32 distance = distanceTo(bounds, eye);
    if (settings.viewDistance > 0.0f && distance > settings.viewDistance)
    {
        return CullResult::TooFar;
    }
    if (sizeCullable && settings.sizeCull > 0.0f && distance > 0.0f)
    {
        const f32 radius = glm::length(bounds.max - bounds.min) * 0.5f;
        if (radius < settings.sizeCull * distance)
        {
            return CullResult::TooSmall;
        }
    }
    return CullResult::Kept;
}

void CullGrid::build(std::span<const AABB> bounds, f32 cellSize)
{
    m_cells.clear();
    m_items.clear();
    // Cell key (x, z) of each object's centre; a map keeps the cell order deterministic.
    std::map<std::pair<i32, i32>, std::vector<u32>> cells;
    for (u32 i = 0; i < bounds.size(); ++i)
    {
        const Vec3 centre = bounds[i].center();
        const auto key = std::pair{static_cast<i32>(std::floor(centre.x / cellSize)),
                                   static_cast<i32>(std::floor(centre.z / cellSize))};
        cells[key].push_back(i);
    }
    for (const auto& [key, items] : cells)
    {
        Cell cell;
        cell.bounds = bounds[items.front()];
        for (const u32 i : items)
        {
            cell.bounds =
                AABB{glm::min(cell.bounds.min, bounds[i].min), glm::max(cell.bounds.max, bounds[i].max)};
        }
        cell.first = static_cast<u32>(m_items.size());
        cell.count = static_cast<u32>(items.size());
        m_items.insert(m_items.end(), items.begin(), items.end());
        m_cells.push_back(cell);
    }
}

void CullGrid::query(const Frustum& frustum, const Vec3& eye, f32 maxDistance, std::vector<u32>& out) const
{
    m_visited = 0;
    for (const Cell& cell : m_cells)
    {
        if ((maxDistance > 0.0f && distanceTo(cell.bounds, eye) > maxDistance) ||
            !frustum.intersects(cell.bounds))
        {
            continue;
        }
        ++m_visited;
        out.insert(out.end(), m_items.begin() + cell.first, m_items.begin() + cell.first + cell.count);
    }
}
} // namespace g7::render

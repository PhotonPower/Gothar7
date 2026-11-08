#include <g7/editor/TerrainBrush.hpp>

#include <algorithm>
#include <cmath>

namespace g7::editor
{
namespace
{
/// Column/row (fractional) of world x, z.
Vec2 gridOf(const world::TerrainRef& ref, f32 x, f32 z) noexcept
{
    return Vec2((x - ref.firstSample.x) / ref.cellSize, (z - ref.firstSample.y) / ref.cellSize);
}

/// World x, z of a map pixel (pixel centres on the samples, first on first, last on last).
Vec2 pixelWorld(const world::TerrainRef& ref, u32 px, u32 py, u32 mapWidth, u32 mapHeight) noexcept
{
    const f32 u = mapWidth > 1 ? static_cast<f32>(px) / static_cast<f32>(mapWidth - 1) : 0.0f;
    const f32 v = mapHeight > 1 ? static_cast<f32>(py) / static_cast<f32>(mapHeight - 1) : 0.0f;
    return Vec2(ref.firstSample.x + u * static_cast<f32>(ref.width - 1) * ref.cellSize,
                ref.firstSample.y + v * static_cast<f32>(ref.height - 1) * ref.cellSize);
}
} // namespace

f32 brushFalloff(f32 distance, f32 radius) noexcept
{
    if (radius <= 0.0f || distance >= radius)
    {
        return 0.0f;
    }
    const f32 t = std::clamp(distance / radius, 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t); // smoothstep down
}

HeightPatch brushArea(const world::Heightfield& field, const Vec3& centre, f32 radius)
{
    const world::TerrainRef& ref = field.ref();
    const Vec2 g = gridOf(ref, centre.x, centre.z);
    const f32 cells = radius / ref.cellSize;
    const i32 c0 = std::max(0, static_cast<i32>(std::floor(g.x - cells)));
    const i32 r0 = std::max(0, static_cast<i32>(std::floor(g.y - cells)));
    const i32 c1 = std::min(static_cast<i32>(ref.width) - 1, static_cast<i32>(std::ceil(g.x + cells)));
    const i32 r1 = std::min(static_cast<i32>(ref.height) - 1, static_cast<i32>(std::ceil(g.y + cells)));
    if (c1 < c0 || r1 < r0)
    {
        return {};
    }
    return {c0, r0, static_cast<u32>(c1 - c0 + 1), static_cast<u32>(r1 - r0 + 1), {}};
}

HeightPatch unite(const HeightPatch& a, const HeightPatch& b)
{
    if (a.width == 0 || a.height == 0)
    {
        return {b.column, b.row, b.width, b.height, {}};
    }
    if (b.width == 0 || b.height == 0)
    {
        return {a.column, a.row, a.width, a.height, {}};
    }
    const i32 c0 = std::min(a.column, b.column);
    const i32 r0 = std::min(a.row, b.row);
    const i32 c1 = std::max(a.column + static_cast<i32>(a.width), b.column + static_cast<i32>(b.width));
    const i32 r1 = std::max(a.row + static_cast<i32>(a.height), b.row + static_cast<i32>(b.height));
    return {c0, r0, static_cast<u32>(c1 - c0), static_cast<u32>(r1 - r0), {}};
}

HeightPatch captureHeights(const world::Heightfield& field, const HeightPatch& area)
{
    return captureHeights(field.samples(), field.ref().width, area);
}

HeightPatch captureHeights(std::span<const u16> samples, u32 fieldWidth, const HeightPatch& area)
{
    HeightPatch patch{area.column, area.row, area.width, area.height, {}};
    patch.samples.reserve(static_cast<usize>(area.width) * area.height);
    for (u32 r = 0; r < area.height; ++r)
    {
        for (u32 c = 0; c < area.width; ++c)
        {
            const usize index = static_cast<usize>(area.row + static_cast<i32>(r)) * fieldWidth +
                                static_cast<usize>(area.column + static_cast<i32>(c));
            patch.samples.push_back(index < samples.size() ? samples[index] : u16{0});
        }
    }
    return patch;
}

void restoreHeights(world::Heightfield& field, const HeightPatch& patch)
{
    for (u32 r = 0; r < patch.height; ++r)
    {
        for (u32 c = 0; c < patch.width; ++c)
        {
            const usize i = static_cast<usize>(r) * patch.width + c;
            if (i < patch.samples.size())
            {
                field.setSample(patch.column + static_cast<i32>(c), patch.row + static_cast<i32>(r),
                                patch.samples[i]);
            }
        }
    }
}

void applyHeightBrush(world::Heightfield& field, const Brush& brush, const Vec3& centre, f32 seconds)
{
    const world::TerrainRef& ref = field.ref();
    const HeightPatch area = brushArea(field, centre, brush.radius);
    if (area.width == 0)
    {
        return;
    }
    // Read from a copy: smoothing must not see this step's own changes.
    const HeightPatch before = captureHeights(field, brushArea(field, centre, brush.radius + ref.cellSize));
    const auto heightBefore = [&](i32 c, i32 r)
    {
        c = std::clamp(c, 0, static_cast<i32>(ref.width) - 1);
        r = std::clamp(r, 0, static_cast<i32>(ref.height) - 1);
        const i32 lc = std::clamp(c - before.column, 0, static_cast<i32>(before.width) - 1);
        const i32 lr = std::clamp(r - before.row, 0, static_cast<i32>(before.height) - 1);
        const u16 v = before.samples[static_cast<usize>(lr) * before.width + static_cast<usize>(lc)];
        return ref.minY + static_cast<f32>(v) / 65535.0f * (ref.maxY - ref.minY);
    };
    for (u32 r = 0; r < area.height; ++r)
    {
        for (u32 c = 0; c < area.width; ++c)
        {
            const i32 col = area.column + static_cast<i32>(c);
            const i32 row = area.row + static_cast<i32>(r);
            const f32 x = ref.firstSample.x + static_cast<f32>(col) * ref.cellSize;
            const f32 z = ref.firstSample.y + static_cast<f32>(row) * ref.cellSize;
            const f32 w = brushFalloff(glm::length(Vec2(x - centre.x, z - centre.z)), brush.radius);
            if (w <= 0.0f)
            {
                continue;
            }
            const f32 y = heightBefore(col, row);
            const f32 share = std::min(1.0f, brush.strength * seconds * w);
            f32 next = y;
            switch (brush.mode)
            {
            case BrushMode::Raise:
                next = y + brush.strength * seconds * w;
                break;
            case BrushMode::Lower:
                next = y - brush.strength * seconds * w;
                break;
            case BrushMode::Smooth:
            {
                f32 sum = 0.0f;
                for (i32 dr = -1; dr <= 1; ++dr)
                {
                    for (i32 dc = -1; dc <= 1; ++dc)
                    {
                        sum += heightBefore(col + dc, row + dr);
                    }
                }
                next = y + (sum / 9.0f - y) * share;
                break;
            }
            case BrushMode::Flatten:
                next = y + (brush.flattenHeight - y) * share;
                break;
            case BrushMode::Paint:
                break;
            }
            field.setSampleHeight(col, row, next);
        }
    }
}

void paintSplat(std::span<SplatPixels> maps, const world::TerrainRef& ref, const Brush& brush,
                const Vec3& centre, f32 seconds)
{
    const u32 target = brush.layer / 4;
    const u32 channel = brush.layer % 4;
    if (target >= maps.size())
    {
        return;
    }
    const SplatPixels& first = maps[target];
    for (u32 py = 0; py < first.height; ++py)
    {
        for (u32 px = 0; px < first.width; ++px)
        {
            const Vec2 at = pixelWorld(ref, px, py, first.width, first.height);
            const f32 w = brushFalloff(glm::length(at - Vec2(centre.x, centre.z)), brush.radius);
            if (w <= 0.0f)
            {
                continue;
            }
            const f32 share = std::min(1.0f, brush.strength * seconds * w);
            // Every layer's weight (all maps, same pixel), towards the painted one; the sum stays.
            for (u32 m = 0; m < maps.size(); ++m)
            {
                if (maps[m].width != first.width || maps[m].height != first.height)
                {
                    continue;
                }
                u8* p = maps[m].rgba.data() + (static_cast<usize>(py) * first.width + px) * 4;
                for (u32 k = 0; k < 4; ++k)
                {
                    const f32 v = static_cast<f32>(p[k]) / 255.0f;
                    const f32 goal = (m == target && k == channel) ? 1.0f : 0.0f;
                    p[k] =
                        static_cast<u8>(std::lround(std::clamp(v + (goal - v) * share, 0.0f, 1.0f) * 255.0f));
                }
            }
        }
    }
}

std::vector<SplatPatch> splatArea(std::span<const SplatPixels> maps, const world::TerrainRef& ref,
                                  const Vec3& centre, f32 radius)
{
    std::vector<SplatPatch> out;
    for (u32 m = 0; m < maps.size(); ++m)
    {
        const SplatPixels& map = maps[m];
        if (map.width < 2 || map.height < 2)
        {
            continue;
        }
        // Pixels per metre along x and z.
        const f32 sx = static_cast<f32>(map.width - 1) / (static_cast<f32>(ref.width - 1) * ref.cellSize);
        const f32 sz = static_cast<f32>(map.height - 1) / (static_cast<f32>(ref.height - 1) * ref.cellSize);
        const f32 cx = (centre.x - ref.firstSample.x) * sx;
        const f32 cz = (centre.z - ref.firstSample.y) * sz;
        const i32 x0 = std::max(0, static_cast<i32>(std::floor(cx - radius * sx)));
        const i32 y0 = std::max(0, static_cast<i32>(std::floor(cz - radius * sz)));
        const i32 x1 =
            std::min(static_cast<i32>(map.width) - 1, static_cast<i32>(std::ceil(cx + radius * sx)));
        const i32 y1 =
            std::min(static_cast<i32>(map.height) - 1, static_cast<i32>(std::ceil(cz + radius * sz)));
        if (x1 < x0 || y1 < y0)
        {
            continue;
        }
        out.push_back({m,
                       static_cast<u32>(x0),
                       static_cast<u32>(y0),
                       static_cast<u32>(x1 - x0 + 1),
                       static_cast<u32>(y1 - y0 + 1),
                       {}});
    }
    return out;
}

SplatPatch captureSplat(std::span<const u8> rgba, u32 mapWidth, const SplatPatch& area)
{
    SplatPatch patch{area.map, area.x, area.y, area.width, area.height, {}};
    for (u32 r = 0; r < area.height; ++r)
    {
        const usize start = (static_cast<usize>(area.y + r) * mapWidth + area.x) * 4;
        patch.rgba.insert(patch.rgba.end(), rgba.begin() + static_cast<std::ptrdiff_t>(start),
                          rgba.begin() +
                              static_cast<std::ptrdiff_t>(start + static_cast<usize>(area.width) * 4));
    }
    return patch;
}

void restoreSplat(std::span<SplatPixels> maps, const SplatPatch& patch)
{
    if (patch.map >= maps.size())
    {
        return;
    }
    SplatPixels& map = maps[patch.map];
    for (u32 r = 0; r < patch.height; ++r)
    {
        const usize to = (static_cast<usize>(patch.y + r) * map.width + patch.x) * 4;
        const usize from = static_cast<usize>(r) * patch.width * 4;
        std::copy_n(patch.rgba.begin() + static_cast<std::ptrdiff_t>(from),
                    static_cast<usize>(patch.width) * 4, map.rgba.begin() + static_cast<std::ptrdiff_t>(to));
    }
}
} // namespace g7::editor

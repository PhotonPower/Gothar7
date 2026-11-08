#include "Heightmap.hpp"

#include <g7/core/Log.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace g7::cook
{
namespace
{
constexpr u32 kMaxSide = 8193; ///< samples per side (a 16 km map at 2 m)
} // namespace

Result<Heightmap> heightmapFromMesh(const asset::MeshData& mesh, f32 cellSize)
{
    if (!(cellSize > 0.0f))
    {
        return Error{"the cell size must be positive"};
    }
    // The triangles of level 0.
    std::vector<std::array<Vec3, 3>> triangles;
    for (const asset::Submesh& s : mesh.submeshes)
    {
        if (s.lod != 0)
        {
            continue;
        }
        for (u32 i = s.firstIndex; i + 2 < s.firstIndex + s.indexCount; i += 3)
        {
            triangles.push_back({mesh.vertices[mesh.indices[i]].position,
                                 mesh.vertices[mesh.indices[i + 1]].position,
                                 mesh.vertices[mesh.indices[i + 2]].position});
        }
    }
    if (triangles.empty())
    {
        return Error{"the mesh has no triangles"};
    }
    Vec3 low(std::numeric_limits<f32>::max());
    Vec3 high(std::numeric_limits<f32>::lowest());
    for (const auto& t : triangles)
    {
        for (const Vec3& p : t)
        {
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
    }
    Heightmap map;
    map.cellSize = cellSize;
    map.firstSample = Vec2(low.x, low.z);
    map.width = static_cast<u32>(std::floor((high.x - low.x) / cellSize)) + 1;
    map.height = static_cast<u32>(std::floor((high.z - low.z) / cellSize)) + 1;
    if (map.width < 2 || map.height < 2 || map.width > kMaxSide || map.height > kMaxSide)
    {
        return Error{std::format("{} x {} samples at {} m: at least 2 and at most {} per side", map.width,
                                 map.height, cellSize, kMaxSide)};
    }
    map.minY = low.y;
    map.maxY = high.y > low.y ? high.y : low.y + 1.0f;
    // The highest surface at each sample (a vertical line through it).
    std::vector<f32> heights(static_cast<usize>(map.width) * map.height, std::numeric_limits<f32>::lowest());
    for (const auto& t : triangles)
    {
        const Vec2 a(t[0].x, t[0].z);
        const Vec2 b(t[1].x, t[1].z);
        const Vec2 c(t[2].x, t[2].z);
        const f32 area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::abs(area) < 1e-12f)
        {
            continue; // vertical or degenerate: no surface seen from above
        }
        const i32 c0 = std::max(
            0, static_cast<i32>(std::ceil((std::min({a.x, b.x, c.x}) - map.firstSample.x) / cellSize)));
        const i32 c1 = std::min(
            static_cast<i32>(map.width) - 1,
            static_cast<i32>(std::floor((std::max({a.x, b.x, c.x}) - map.firstSample.x) / cellSize)));
        const i32 r0 = std::max(
            0, static_cast<i32>(std::ceil((std::min({a.y, b.y, c.y}) - map.firstSample.y) / cellSize)));
        const i32 r1 = std::min(
            static_cast<i32>(map.height) - 1,
            static_cast<i32>(std::floor((std::max({a.y, b.y, c.y}) - map.firstSample.y) / cellSize)));
        for (i32 r = r0; r <= r1; ++r)
        {
            for (i32 col = c0; col <= c1; ++col)
            {
                const Vec2 p(map.firstSample.x + static_cast<f32>(col) * cellSize,
                             map.firstSample.y + static_cast<f32>(r) * cellSize);
                const f32 w1 = ((c.x - p.x) * (a.y - p.y) - (a.x - p.x) * (c.y - p.y)) / area;
                const f32 w2 = ((a.x - p.x) * (b.y - p.y) - (b.x - p.x) * (a.y - p.y)) / area;
                const f32 w0 = 1.0f - w1 - w2;
                constexpr f32 kEdge = -1e-5f; // samples on an edge count for both triangles
                if (w0 < kEdge || w1 < kEdge || w2 < kEdge)
                {
                    continue;
                }
                const f32 y = w0 * t[0].y + w1 * t[1].y + w2 * t[2].y;
                f32& h = heights[static_cast<usize>(r) * map.width + static_cast<usize>(col)];
                h = std::max(h, y);
            }
        }
    }
    map.samples.reserve(heights.size());
    for (const f32 h : heights)
    {
        if (h == std::numeric_limits<f32>::lowest())
        {
            ++map.uncovered;
        }
        const f32 y = h == std::numeric_limits<f32>::lowest() ? map.minY : h;
        const f32 t = std::clamp((y - map.minY) / (map.maxY - map.minY), 0.0f, 1.0f);
        map.samples.push_back(static_cast<u16>(std::lround(t * 65535.0f)));
    }
    return map;
}

std::string terrainBlockJson(const Heightmap& map, std::string_view heightmapPath)
{
    return std::format(
        "\"terrain\": {{\"version\":1,\"heightmap\":\"{}\",\"width\":{},\"height\":{},\"cellSize\":{},"
        "\"firstSample\":[{},{}],\"minY\":{},\"maxY\":{}}}",
        heightmapPath, map.width, map.height, map.cellSize, map.firstSample.x, map.firstSample.y, map.minY,
        map.maxY);
}

Result<Heightmap> cookHeightmap(const fs::Path& mesh, const fs::Path& out, f32 cellSize,
                                std::string_view heightmapPath)
{
    auto data = asset::loadGltf(mesh);
    if (!data)
    {
        return data.error();
    }
    auto map = heightmapFromMesh(data.value(), cellSize);
    if (!map)
    {
        return map.error();
    }
    std::vector<u8> bytes;
    bytes.reserve(map.value().samples.size() * 2);
    for (const u16 v : map.value().samples)
    {
        bytes.push_back(static_cast<u8>(v & 0xFF)); // little endian (world.md)
        bytes.push_back(static_cast<u8>(v >> 8));
    }
    if (auto written = fs::writeFileAtomic(out, bytes); !written)
    {
        return written.error();
    }
    fs::Path block = out;
    block += ".terrain.json";
    if (auto written = fs::writeText(block, "{" + terrainBlockJson(map.value(), heightmapPath) + "}\n");
        !written)
    {
        return written.error();
    }
    if (map.value().uncovered > 0)
    {
        G7_LOG_WARN("cook", "{}: {} samples without a surface above or below - at the lowest height",
                    fs::toUtf8(mesh), map.value().uncovered);
    }
    G7_LOG_INFO("cook", "heightmap {} x {} ({} m cells, {:.2f} .. {:.2f} m) -> {}", map.value().width,
                map.value().height, cellSize, map.value().minY, map.value().maxY, fs::toUtf8(out));
    return map;
}
} // namespace g7::cook

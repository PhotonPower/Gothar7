// Mesh -> heightmap (M16 part F2, ADR 0023): a box with a sloped top rastered onto a 1 m grid - the top wins
// over the bottom, the slope is exact at the samples; a box turned by 45 degrees leaves its bounds' corners
// uncovered.

#include "Heightmap.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <ostream>
#include <string>

using namespace g7;
using namespace g7::cook;

namespace
{
/// A box over x, z in [0, 10] whose top rises y = 2 + 0.5 x, bottom at 0; turned by `yaw` about the centre.
asset::MeshData skewedBox(f32 yawDegrees = 0.0f)
{
    const f32 yaw = yawDegrees * 3.14159265f / 180.0f;
    const auto turn = [&](f32 x, f32 y, f32 z)
    {
        const f32 dx = x - 5.0f;
        const f32 dz = z - 5.0f;
        return Vec3(5.0f + dx * std::cos(yaw) + dz * std::sin(yaw), y,
                    5.0f - dx * std::sin(yaw) + dz * std::cos(yaw));
    };
    asset::MeshData mesh;
    const Vec3 corners[8] = {turn(0, 0, 0), turn(10, 0, 0), turn(10, 0, 10), turn(0, 0, 10),
                             turn(0, 2, 0), turn(10, 7, 0), turn(10, 7, 10), turn(0, 2, 10)};
    for (const Vec3& c : corners)
    {
        mesh.vertices.push_back({c});
    }
    // bottom, top, and the four sides (vertical: they add nothing seen from above)
    mesh.indices = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                    1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0};
    mesh.submeshes.push_back({0, static_cast<u32>(mesh.indices.size()), 0, 0});
    return mesh;
}

f32 heightOf(const Heightmap& map, u32 column, u32 row)
{
    const u16 v = map.samples[static_cast<usize>(row) * map.width + column];
    return map.minY + static_cast<f32>(v) / 65535.0f * (map.maxY - map.minY);
}
} // namespace

TEST_CASE("Heightmap from mesh: a box with a sloped top")
{
    auto map = heightmapFromMesh(skewedBox(), 1.0f);
    REQUIRE(map.ok());
    const Heightmap& m = map.value();
    CHECK(m.width == 11);
    CHECK(m.height == 11);
    CHECK(m.firstSample == Vec2(0.0f));
    CHECK(m.minY == doctest::Approx(0.0f));
    CHECK(m.maxY == doctest::Approx(7.0f));
    CHECK(m.uncovered == 0);
    for (u32 c = 0; c <= 10; ++c)
    {
        CHECK(heightOf(m, c, 5) ==
              doctest::Approx(2.0f + 0.5f * static_cast<f32>(c)).epsilon(0.001)); // the top
    }
    CHECK(heightOf(m, 3, 0) == doctest::Approx(heightOf(m, 3, 10))); // the slope runs along x only

    // 2 m cells: 6 x 6 samples.
    auto coarse = heightmapFromMesh(skewedBox(), 2.0f);
    REQUIRE(coarse.ok());
    CHECK(coarse.value().width == 6);
    CHECK_FALSE(heightmapFromMesh(skewedBox(), 0.0f).ok());
    CHECK_FALSE(heightmapFromMesh(asset::MeshData{}, 1.0f).ok());
}

TEST_CASE("Heightmap from mesh: a turned box leaves its bounds' corners uncovered; the terrain block")
{
    auto map = heightmapFromMesh(skewedBox(45.0f), 0.5f);
    REQUIRE(map.ok());
    CHECK(map.value().uncovered > 0);
    CHECK(map.value().samples[0] == 0); // uncovered: the lowest height
    const std::string block = terrainBlockJson(map.value(), "worlds/hill/terrain.r16");
    CHECK(block.starts_with("\"terrain\": {\"version\":1,\"heightmap\":\"worlds/hill/terrain.r16\""));
    CHECK(block.find("\"cellSize\":0.5") != std::string::npos);
}

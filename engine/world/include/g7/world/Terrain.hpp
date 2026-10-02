#pragma once

// Heightmap terrain of a world (M4): the "terrain" block of .g7world and the heights it points to.
// Contract with the world track: docs/modules/world.md ("Gelände"), docs/coordination.md.

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Terrain.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
class Vfs;
}

namespace g7::world
{
inline constexpr u32 kTerrainVersion = 1;

/// The "terrain" block of a .g7world (optional; without it a world has no terrain).
struct TerrainRef
{
    std::string heightmap;  ///< VFS path of the .r16 file: uint16 little endian, row by row
    u32 width = 0;          ///< samples per row
    u32 height = 0;         ///< rows
    f32 cellSize = 1.0f;    ///< metres between sample centres
    Vec2 firstSample{0.0f}; ///< x, z of the centre of column 0 / row 0 (row 0 = smallest z = north)
    f32 minY = 0.0f;
    f32 maxY = 1.0f;
};

/// Heights in metres from a 16-bit value and back (the contract's encoding; halves may round
/// either way when encoding).
[[nodiscard]] f32 decodeHeight(u16 value, f32 minY, f32 maxY) noexcept;
[[nodiscard]] u16 encodeHeight(f32 height, f32 minY, f32 maxY) noexcept;

/// A loaded heightfield: heights for gameplay and physics, and the description for rendering.
class Heightfield
{
public:
    Heightfield() = default;
    /// Checks the block and the sample count against the data.
    [[nodiscard]] static Result<Heightfield> create(const TerrainRef& ref, std::vector<u16> samples);
    [[nodiscard]] static Result<Heightfield> load(const asset::Vfs& vfs, const TerrainRef& ref);

    /// Height at world x, z: bilinear between sample centres; outside the area the edge height.
    [[nodiscard]] f32 heightAt(f32 x, f32 z) const noexcept;
    /// Surface normal at world x, z (from the slope of neighbouring samples).
    [[nodiscard]] Vec3 normalAt(f32 x, f32 z) const noexcept;
    /// Height of sample (column, row), clamped to the grid.
    [[nodiscard]] f32 sampleHeight(i32 column, i32 row) const noexcept;
    /// From the first to the last sample centre, minY to maxY.
    [[nodiscard]] AABB bounds() const noexcept;

    [[nodiscard]] const TerrainRef& ref() const noexcept { return m_ref; }
    [[nodiscard]] render::HeightfieldDesc renderDesc() const noexcept;

private:
    TerrainRef m_ref;
    std::vector<u16> m_samples;
};
} // namespace g7::world

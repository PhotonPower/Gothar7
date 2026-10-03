#pragma once

// Heightmap terrain of a world (M4): the heights the "terrain" block (TerrainRef.hpp) points to.
// Contract with the world track: docs/modules/world.md ("Gelände"), docs/coordination.md.

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Terrain.hpp>
#include <g7/world/TerrainRef.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
class Vfs;
}

namespace g7::world
{

/// A loaded heightfield: heights for gameplay and physics, and the description for rendering.
class Heightfield
{
public:
    Heightfield() = default;
    /// Checks the block and the sample count against the data.
    [[nodiscard]] static Result<Heightfield> create(const TerrainRef& ref, std::vector<u16> samples);
    /// Checks the hole mask (one byte per cell); empty = no holes.
    [[nodiscard]] Result<void> setHoles(std::vector<u8> holes);
    [[nodiscard]] static Result<Heightfield> load(const asset::Vfs& vfs, const TerrainRef& ref);
    /// Loads the heights and, if the block names one, the hole mask (a mask of only zeros is
    /// accepted with a warning: it removes the whole terrain).

    /// Height at world x, z: bilinear between sample centres; outside the area the edge height.
    [[nodiscard]] f32 heightAt(f32 x, f32 z) const noexcept;
    /// Surface normal at world x, z (from the slope of neighbouring samples).
    [[nodiscard]] Vec3 normalAt(f32 x, f32 z) const noexcept;
    /// Height of sample (column, row), clamped to the grid.
    [[nodiscard]] f32 sampleHeight(i32 column, i32 row) const noexcept;
    /// True if x, z lies in a cell the hole mask removes (outside the area: false).
    [[nodiscard]] bool isHole(f32 x, f32 z) const noexcept;
    [[nodiscard]] std::span<const u8> holes() const noexcept { return m_holes; }
    /// From the first to the last sample centre, minY to maxY.
    [[nodiscard]] AABB bounds() const noexcept;

    [[nodiscard]] const TerrainRef& ref() const noexcept { return m_ref; }
    [[nodiscard]] render::HeightfieldDesc renderDesc() const noexcept;

private:
    TerrainRef m_ref;
    std::vector<u16> m_samples;
    std::vector<u8> m_holes; // empty or (width - 1) * (height - 1)
};
} // namespace g7::world

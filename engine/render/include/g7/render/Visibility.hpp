#pragma once

// Visibility (M4): a grid over the scene for frustum queries, and hiding by distance and on-screen
// size. Plain CPU code: the engine owns the instances, this only answers which ones to draw.

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>

#include <span>
#include <vector>

namespace g7::render
{
/// engine.toml [render]: view_distance and size_cull (0 = off).
struct CullSettings
{
    f32 viewDistance = 0.0f; ///< metres from the camera to the nearest point of the bounds
    /// Hides size-cullable objects whose bounding radius divided by their distance is below this
    /// (0.005 at 60 degrees vertical field of view and 900 pixels is a radius of about 4 pixels).
    f32 sizeCull = 0.0f;
};

enum class CullResult : u8
{
    Kept,
    TooFar,   ///< beyond viewDistance
    TooSmall, ///< below sizeCull (size-cullable objects only)
};

/// Levels of detail of static models by distance (asset.md "Detailstufen (LOD) statischer Modelle").
struct LodSettings
{
    f32 lod1Distance = 60.0f; ///< metres from the camera to the centre of the bounds
    f32 lod2Distance = 150.0f;
    f32 hysteresis = 0.1f; ///< share of a threshold to go past it before switching back (no flicker)
    i32 forced = -1;       ///< 0, 1, 2: every object at this level (--lod, debug UI); -1: by distance
};

/// The level for `distance`, starting from `current`: coarser beyond threshold * (1 + hysteresis), finer
/// below threshold * (1 - hysteresis); between them the current level stays.
[[nodiscard]] u32 selectLod(f32 distance, u32 current, const LodSettings& settings) noexcept;

/// Distance and size test for bounds seen from `eye`; frustum tests are separate.
[[nodiscard]] CullResult cullByDistance(const AABB& bounds, const Vec3& eye, const CullSettings& settings,
                                        bool sizeCullable) noexcept;

/// Objects sorted into square cells over x/z, so a frustum test visits cells first and only the
/// objects of cells it reaches. Cell bounds grow to whatever their objects cover (an object belongs
/// to the cell of its centre), so large objects are never missed.
class CullGrid
{
public:
    /// Sorts `bounds` (index = object) into cells of `cellSize` metres.
    void build(std::span<const AABB> bounds, f32 cellSize = 64.0f);
    /// Appends the indices of objects in cells that intersect `frustum` and lie within `maxDistance`
    /// of `eye` (0 = no limit); the objects themselves are not tested.
    void query(const Frustum& frustum, const Vec3& eye, f32 maxDistance, std::vector<u32>& out) const;

    [[nodiscard]] usize cellCount() const noexcept { return m_cells.size(); }
    /// Cells visited by the last query (after the frustum and distance test).
    [[nodiscard]] usize visitedCells() const noexcept { return m_visited; }

private:
    struct Cell
    {
        AABB bounds;
        u32 first = 0; ///< range in m_items
        u32 count = 0;
    };
    std::vector<Cell> m_cells; // non-empty cells only
    std::vector<u32> m_items;
    mutable usize m_visited = 0;
};

/// Distance from `point` to the nearest point of `box` (0 inside).
[[nodiscard]] f32 distanceTo(const AABB& box, const Vec3& point) noexcept;
} // namespace g7::render

#pragma once

// The height brush (M16 part F2, ADR 0023): raise, lower, smooth, flatten to a height, paint a splat layer -
// plain functions on a heightfield and the splat maps' RGBA bytes, tested without a window. Undo keeps the
// rectangle a stroke touched, before and after (a height delta, not the whole terrain).

#include <g7/core/Math.hpp>
#include <g7/world/Terrain.hpp>

#include <span>
#include <vector>

namespace g7::editor
{
enum class BrushMode : u8
{
    Raise,
    Lower,
    Smooth,
    Flatten, ///< towards `flattenHeight`
    Paint,   ///< the splat layer `layer`
};

struct Brush
{
    BrushMode mode = BrushMode::Raise;
    f32 radius = 4.0f;   ///< m
    f32 strength = 2.0f; ///< raise/lower: m per second at the centre; smooth/flatten/paint: share per second
    f32 flattenHeight = 0.0f;
    u32 layer = 0; ///< paint: layer 4m + k = channel k of map m
};

/// Weight at `distance` from the centre: 1 there, falling smoothly to 0 at the radius.
[[nodiscard]] f32 brushFalloff(f32 distance, f32 radius) noexcept;

/// A rectangle of samples (columns `column` .. +width, rows `row` .. +height) and, captured, their values.
struct HeightPatch
{
    i32 column = 0;
    i32 row = 0;
    u32 width = 0;
    u32 height = 0;
    std::vector<u16> samples; ///< row by row; empty: only the rectangle

    friend bool operator==(const HeightPatch&, const HeightPatch&) = default;
};

/// The samples a brush of `radius` at `centre` reaches (clamped to the grid; empty outside).
[[nodiscard]] HeightPatch brushArea(const world::Heightfield& field, const Vec3& centre, f32 radius);
/// The rectangle holding both.
[[nodiscard]] HeightPatch unite(const HeightPatch& a, const HeightPatch& b);
[[nodiscard]] HeightPatch captureHeights(const world::Heightfield& field, const HeightPatch& area);
/// The same rectangle, values from `samples` (a full copy of the field's samples, e.g. from before a stroke).
[[nodiscard]] HeightPatch captureHeights(std::span<const u16> samples, u32 fieldWidth,
                                         const HeightPatch& area);
void restoreHeights(world::Heightfield& field, const HeightPatch& patch);

/// One step of the height brush (raise, lower, smooth, flatten) over `seconds`.
void applyHeightBrush(world::Heightfield& field, const Brush& brush, const Vec3& centre, f32 seconds);

/// A splat map's bytes (RGBA8, pixel centres on the samples: the first pixel on the first sample).
struct SplatPixels
{
    std::span<u8> rgba;
    u32 width = 0;
    u32 height = 0;
};

/// One step of painting `brush.layer` over `seconds`: its weight grows, the other layers' weights (in all
/// maps) shrink so that the sum stays.
void paintSplat(std::span<SplatPixels> maps, const world::TerrainRef& ref, const Brush& brush,
                const Vec3& centre, f32 seconds);

/// A rectangle of one splat map's pixels and their bytes (undo of painting).
struct SplatPatch
{
    u32 map = 0;
    u32 x = 0;
    u32 y = 0;
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;

    friend bool operator==(const SplatPatch&, const SplatPatch&) = default;
};
/// Every map's pixels a brush at `centre` reaches (the whole maps' rectangles as one per map).
[[nodiscard]] std::vector<SplatPatch> splatArea(std::span<const SplatPixels> maps,
                                                const world::TerrainRef& ref, const Vec3& centre, f32 radius);
[[nodiscard]] SplatPatch captureSplat(std::span<const u8> rgba, u32 mapWidth, const SplatPatch& area);
void restoreSplat(std::span<SplatPixels> maps, const SplatPatch& patch);
} // namespace g7::editor

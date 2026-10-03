#pragma once

// The "terrain" block of .g7world and the height encoding (world_format: no rendering, no physics).
// Contract with the world track: docs/modules/world.md ("Gelände"), docs/coordination.md.

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <string>
#include <vector>

namespace g7::world
{
inline constexpr u32 kTerrainVersion = 1;

inline constexpr u32 kMaxTerrainLayers = 8;

/// One splat layer of the terrain block.
struct TerrainLayerRef
{
    std::string name;
    std::string albedo; ///< VFS path, sRGB colour (all layers the same size)
    f32 tile = 4.0f;    ///< metres per texture repeat
    std::string normal; ///< VFS path, reserved: read and written, not yet drawn
};

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
    /// "splat": 1 or 2 RGBA maps (linear data; channel k of map m = weight of layer 4m + k) and up to 8
    /// layers. Pixel centres lie on samples: first pixel on the first sample, last on the last.
    std::vector<std::string> splatMaps;
    std::vector<TerrainLayerRef> layers;
    /// "holes": VFS path of a raw 8-bit mask, one byte per cell ((width - 1) * (height - 1), row 0 =
    /// north); 0 = hole. Empty = no holes.
    std::string holes;
};

/// Heights in metres from a 16-bit value and back (the contract's encoding; halves may round
/// either way when encoding).
[[nodiscard]] f32 decodeHeight(u16 value, f32 minY, f32 maxY) noexcept;
[[nodiscard]] u16 encodeHeight(f32 height, f32 minY, f32 maxY) noexcept;
} // namespace g7::world

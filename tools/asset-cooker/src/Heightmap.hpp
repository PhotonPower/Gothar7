#pragma once

// Mesh -> heightmap (M16 part F2, ADR 0023): large terrain forms modelled in Blender become the terrain
// block's .r16 heightmap (world.md "Gelände"). Each sample takes the highest surface straight above or below
// it; samples no triangle covers get the lowest height (and are counted).

#include <g7/asset/MeshData.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>

#include <string>
#include <vector>

namespace g7::cook
{
struct Heightmap
{
    u32 width = 0;  ///< samples per row
    u32 height = 0; ///< rows (row 0 = smallest z)
    f32 cellSize = 1.0f;
    Vec2 firstSample{0.0f};
    f32 minY = 0.0f;
    f32 maxY = 1.0f;
    std::vector<u16> samples; ///< row by row, minY .. maxY as 0 .. 65535
    usize uncovered = 0;      ///< samples without a triangle above or below
};

/// Rasters the mesh's triangles (level 0, not the COL_ parts) onto a grid of `cellSize` metres over their
/// x/z bounds.
[[nodiscard]] Result<Heightmap> heightmapFromMesh(const asset::MeshData& mesh, f32 cellSize);

/// The terrain block (world.md, version 1) for `heightmapPath` (VFS path of the written .r16), one line.
[[nodiscard]] std::string terrainBlockJson(const Heightmap& map, std::string_view heightmapPath);

/// Loads the glTF, rasters it and writes `<out>` (.r16, little endian) and `<out>.terrain.json` (the block).
[[nodiscard]] Result<Heightmap> cookHeightmap(const fs::Path& mesh, const fs::Path& out, f32 cellSize,
                                              std::string_view heightmapPath);
} // namespace g7::cook

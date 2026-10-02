#pragma once

// Heightmap terrain (M4): chunks drawn from shared grids, heights fetched in the vertex shader.
// render knows only this description; world turns a .g7world "terrain" block into it.

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Shadows.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <array>
#include <span>
#include <vector>

namespace g7::render
{
class Device;
class ShaderLibrary;
class LightList;
struct Camera;

/// A regular grid of heights. Sample (column c, row r) lies at x = firstSample.x + c * cellSize,
/// z = firstSample.y + r * cellSize; its height is minY + v / 65535 * (maxY - minY).
struct HeightfieldDesc
{
    u32 width = 0;  ///< samples per row (>= 2)
    u32 height = 0; ///< rows (>= 2)
    f32 cellSize = 1.0f;
    Vec2 firstSample{0.0f}; ///< x, z of sample (0, 0)
    f32 minY = 0.0f;
    f32 maxY = 1.0f;
    std::span<const u16> samples; ///< width * height, row by row
};

class TerrainRenderer
{
public:
    static constexpr u32 kChunkCells = 64; ///< cells per chunk side at full detail
    static constexpr u32 kLodLevels = 4;   ///< grids with 1, 2, 4, 8 samples per vertex

    TerrainRenderer() = default;
    [[nodiscard]] static Result<TerrainRenderer> create(Device& device, ShaderLibrary& shaders,
                                                        const HeightfieldDesc& heightfield,
                                                        const ShadowSettings& shadows = {});

    /// Depth of every chunk inside the cascade's light volume (the terrain casts shadows too).
    void drawShadow(Device& device, const Cascade& cascade);
    /// Main pass: chunks inside the camera frustum, each at the detail its distance asks for. The
    /// lighting block and shadow atlas must be bound (MeshRenderer::bindLighting).
    void draw(Device& device, const Camera& camera, const LightList* lights);

    /// Detail level for a chunk `distance` metres away: 0 within lodDistance, then one level per
    /// doubling, at most kLodLevels - 1.
    [[nodiscard]] static u32 lodFor(f32 distance, f32 lodDistance) noexcept;

    [[nodiscard]] const AABB& bounds() const noexcept { return m_bounds; }
    [[nodiscard]] usize chunkCount() const noexcept { return m_chunks.size(); }
    /// Chunks drawn in the last main pass (after culling).
    [[nodiscard]] u32 drawnChunks() const noexcept { return m_drawn; }

    f32 lodDistance = 96.0f; ///< metres of full detail; each LOD level doubles it

private:
    struct Chunk
    {
        Vec2 origin; ///< first sample (column, row)
        AABB bounds;
    };
    struct Grid
    {
        rhi::Buffer vertices;
        rhi::Buffer indices;
        u32 indexCount = 0;
    };

    void bindCommon(rhi::ShaderProgram& program, Device& device);

    std::vector<Chunk> m_chunks;
    std::array<Grid, kLodLevels> m_grids;
    rhi::Texture m_heights;
    rhi::Sampler m_nearest;
    rhi::ShaderProgram* m_program = nullptr; // owned by the ShaderLibrary
    rhi::ShaderProgram* m_shadowProgram = nullptr;
    rhi::Pipeline m_pipeline;
    rhi::Pipeline m_shadowPipeline;
    HeightfieldDesc m_desc; // samples not kept (they live in the texture)
    AABB m_bounds;
    f32 m_skirtDepth = 1.0f;
    u32 m_drawn = 0;
    std::vector<u32> m_selectedLights;
};
} // namespace g7::render

#pragma once

// Heightmap terrain (M4): chunks drawn from shared grids, heights fetched in the vertex shader.
// render knows only this description; world turns a .g7world "terrain" block into it.

#include <g7/asset/TextureData.hpp>
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

/// Surface of the terrain: splat weights, layer textures and holes (all optional).
struct TerrainSurfaceDesc
{
    static constexpr u32 kMaxLayers = 8;
    struct Layer
    {
        const asset::TextureData* albedo = nullptr; ///< sRGB colour; all layers the same size and format
        f32 tile = 4.0f;                            ///< metres per texture repeat
    };
    /// 1 or 2 maps (same size and format): channel k of map m = weight of layer 4m + k. Linear data. The
    /// centre of the first pixel lies on the first sample, that of the last pixel on the last sample.
    std::vector<const asset::TextureData*> splatMaps;
    std::vector<Layer> layers;
    /// (width - 1) * (height - 1) bytes, one per cell, row 0 = north; 0 = hole. Empty = no holes.
    std::span<const u8> holes;
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

    /// Sets (or replaces) splat layers and holes. Fails with a message for more than 8 layers, too
    /// few maps, layers or maps that differ in size or format, and a hole mask of the wrong size;
    /// the previous surface stays then.
    [[nodiscard]] Result<void> setSurface(Device& device, const TerrainSurfaceDesc& surface);
    [[nodiscard]] u32 layerCount() const noexcept { return m_layerCount; }
    [[nodiscard]] bool hasHoles() const noexcept { return m_hasHoles; }

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
    rhi::Texture m_splat;    // 2D array, 2 layers (weights of 8 layers)
    rhi::Texture m_layers;   // 2D array of layer albedos
    rhi::Texture m_holes;    // R8, one texel per cell
    rhi::Texture m_noLayers; // 1x1 placeholders: every sampler of the shaders sees a complete texture
    rhi::Texture m_noHoles;
    rhi::Sampler m_linearClamp;
    rhi::Sampler m_linearRepeat;
    std::array<f32, TerrainSurfaceDesc::kMaxLayers> m_tiles{};
    Vec4 m_splatTransform{1.0f, 1.0f, 0.0f, 0.0f};
    u32 m_layerCount = 0;
    bool m_hasHoles = false;
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

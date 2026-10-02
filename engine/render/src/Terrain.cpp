#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Terrain.hpp>
#include <g7/render/TextureUpload.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace g7::render
{
namespace
{
constexpr u32 kHeightUnit = 4; // texture units: 0-2 materials, 3 shadow atlas, 4-7 terrain
constexpr u32 kSplatUnit = 5;
constexpr u32 kLayerUnit = 6;
constexpr u32 kHoleUnit = 7;

/// Grid vertex: x, z in samples relative to the chunk origin; y = 1 for skirt vertices.
struct GridVertex
{
    f32 x, skirt, z;
};

/// Vertices and indices of one chunk grid with `step` samples between vertices, plus a skirt (a
/// strip hanging down along the border) that hides cracks between chunks of different detail.
void buildGrid(u32 step, std::vector<GridVertex>& vertices, std::vector<u32>& indices)
{
    const u32 n = TerrainRenderer::kChunkCells / step; // quads per side
    const u32 side = n + 1;
    vertices.clear();
    indices.clear();
    for (u32 j = 0; j < side; ++j)
    {
        for (u32 i = 0; i < side; ++i)
        {
            vertices.push_back({f32(i * step), 0.0f, f32(j * step)});
        }
    }
    const auto at = [side](u32 i, u32 j) { return j * side + i; };
    // Counter-clockwise seen from above (+Y): (i, j), (i, j + 1), (i + 1, j) ...
    for (u32 j = 0; j < n; ++j)
    {
        for (u32 i = 0; i < n; ++i)
        {
            indices.insert(indices.end(), {at(i, j), at(i, j + 1), at(i + 1, j), at(i + 1, j), at(i, j + 1),
                                           at(i + 1, j + 1)});
        }
    }
    // Skirt: for each border edge a quad from the edge down to a lowered copy.
    const auto skirtEdge = [&](u32 a, u32 b)
    {
        const auto lowA = static_cast<u32>(vertices.size());
        vertices.push_back({vertices[a].x, 1.0f, vertices[a].z});
        vertices.push_back({vertices[b].x, 1.0f, vertices[b].z});
        indices.insert(indices.end(), {a, lowA, b, b, lowA, lowA + 1});
    };
    for (u32 k = 0; k < n; ++k)
    {
        skirtEdge(at(k, 0), at(k + 1, 0));
        skirtEdge(at(k + 1, n), at(k, n));
        skirtEdge(at(0, k + 1), at(0, k));
        skirtEdge(at(n, k), at(n, k + 1));
    }
}

template <typename T>
std::span<const u8> bytesOf(const std::vector<T>& v)
{
    return {reinterpret_cast<const u8*>(v.data()), v.size() * sizeof(T)};
}
} // namespace

u32 TerrainRenderer::lodFor(f32 distance, f32 lodDistance) noexcept
{
    if (lodDistance <= 0.0f || distance < lodDistance)
    {
        return 0;
    }
    const auto level = static_cast<u32>(std::floor(std::log2(distance / lodDistance))) + 1;
    return std::min(level, kLodLevels - 1);
}

Result<TerrainRenderer> TerrainRenderer::create(Device& device, ShaderLibrary& shaders,
                                                const HeightfieldDesc& heightfield,
                                                const ShadowSettings& shadows)
{
    const HeightfieldDesc& d = heightfield;
    if (d.width < 2 || d.height < 2 || d.cellSize <= 0.0f || !(d.maxY > d.minY) ||
        d.samples.size() != static_cast<usize>(d.width) * d.height)
    {
        return Error{"invalid heightfield (" + std::to_string(d.width) + " x " + std::to_string(d.height) +
                     ", " + std::to_string(d.samples.size()) + " samples)"};
    }
    TerrainRenderer terrain;
    terrain.m_desc = d;
    terrain.m_desc.samples = {};

    auto program = shaders.load("terrain", {"terrain.vert", "terrain.frag", {}});
    auto shadowProgram = shaders.load("terrain_shadow", {"terrain.vert", "terrain.frag", {"SHADOW"}});
    if (!program || !shadowProgram)
    {
        return !program ? program.error() : shadowProgram.error();
    }
    terrain.m_program = program.value();
    terrain.m_shadowProgram = shadowProgram.value();

    rhi::PipelineDesc desc;
    desc.attributes = {{0, rhi::VertexFormat::Float3, 0}};
    desc.vertexStride = sizeof(GridVertex);
    desc.cull = rhi::CullMode::None; // skirts are seen from both sides
    desc.program = terrain.m_program;
    auto pipeline = device.createPipeline(desc);
    desc.program = terrain.m_shadowProgram;
    desc.depthCompare = rhi::CompareOp::LessEqual; // shadow depth is not reversed
    desc.depthBias = {shadows.depthBias, shadows.slopeBias};
    auto shadowPipeline = device.createPipeline(desc);
    if (!pipeline || !shadowPipeline)
    {
        return !pipeline ? pipeline.error() : shadowPipeline.error();
    }
    terrain.m_pipeline = std::move(pipeline).value();
    terrain.m_shadowPipeline = std::move(shadowPipeline).value();

    // Heights: one R16 texture, read with texelFetch (no filtering, no mipmaps).
    auto texture = device.createTexture({d.width, d.height, rhi::Format::R16, 1});
    if (!texture)
    {
        return texture.error();
    }
    terrain.m_heights = std::move(texture).value();
    if (auto uploaded = terrain.m_heights.upload(
            0, std::span<const u8>(reinterpret_cast<const u8*>(d.samples.data()), d.samples.size_bytes()));
        !uploaded)
    {
        return uploaded.error();
    }
    rhi::SamplerDesc nearest;
    nearest.minFilter = nearest.magFilter = nearest.mipFilter = rhi::Filter::Nearest;
    nearest.wrapU = nearest.wrapV = rhi::Wrap::Clamp;
    auto sampler = device.createSampler(nearest);
    if (!sampler)
    {
        return sampler.error();
    }
    terrain.m_nearest = std::move(sampler).value();
    rhi::SamplerDesc clamp;
    clamp.wrapU = clamp.wrapV = rhi::Wrap::Clamp;
    rhi::SamplerDesc repeat;
    repeat.maxAnisotropy = 8.0f; // layers are seen at grazing angles
    auto linearClamp = device.createSampler(clamp);
    auto linearRepeat = device.createSampler(repeat);
    if (!linearClamp || !linearRepeat)
    {
        return !linearClamp ? linearClamp.error() : linearRepeat.error();
    }
    terrain.m_linearClamp = std::move(linearClamp).value();
    terrain.m_linearRepeat = std::move(linearRepeat).value();
    auto noLayers = device.createTexture({1, 1, rhi::Format::RGBA8, 1, 1, true});
    auto noHoles = device.createTexture({1, 1, rhi::Format::R8, 1});
    if (!noLayers || !noHoles)
    {
        return !noLayers ? noLayers.error() : noHoles.error();
    }
    const std::array<u8, 4> white{255, 255, 255, 255};
    if (auto a = noLayers.value().upload(0, white); !a)
    {
        return a.error();
    }
    if (auto b = noHoles.value().upload(0, std::span<const u8>(white).first(1)); !b)
    {
        return b.error();
    }
    terrain.m_noLayers = std::move(noLayers).value();
    terrain.m_noHoles = std::move(noHoles).value();

    std::vector<GridVertex> vertices;
    std::vector<u32> indices;
    for (u32 level = 0; level < kLodLevels; ++level)
    {
        buildGrid(1u << level, vertices, indices);
        auto vb = device.createBuffer(
            {vertices.size() * sizeof(GridVertex), rhi::BufferUsage::Static, bytesOf(vertices)});
        auto ib =
            device.createBuffer({indices.size() * sizeof(u32), rhi::BufferUsage::Static, bytesOf(indices)});
        if (!vb || !ib)
        {
            return Error{"cannot create terrain grid buffers"};
        }
        terrain.m_grids[level] = {std::move(vb).value(), std::move(ib).value(),
                                  static_cast<u32>(indices.size())};
    }

    // Chunks with their height range (for culling); skirts hang a little below the lowest point.
    const f32 range = d.maxY - d.minY;
    terrain.m_skirtDepth = std::max(d.cellSize * 4.0f, range * 0.01f);
    const u32 chunksX = (d.width - 1 + kChunkCells - 1) / kChunkCells;
    const u32 chunksZ = (d.height - 1 + kChunkCells - 1) / kChunkCells;
    for (u32 cz = 0; cz < chunksZ; ++cz)
    {
        for (u32 cx = 0; cx < chunksX; ++cx)
        {
            const u32 x0 = cx * kChunkCells;
            const u32 z0 = cz * kChunkCells;
            const u32 x1 = std::min(x0 + kChunkCells, d.width - 1);
            const u32 z1 = std::min(z0 + kChunkCells, d.height - 1);
            u16 low = 65535;
            u16 high = 0;
            for (u32 z = z0; z <= z1; ++z)
            {
                for (u32 x = x0; x <= x1; ++x)
                {
                    const u16 v = d.samples[static_cast<usize>(z) * d.width + x];
                    low = std::min(low, v);
                    high = std::max(high, v);
                }
            }
            const auto heightOf = [&](u16 v) { return d.minY + static_cast<f32>(v) / 65535.0f * range; };
            const Vec3 min(d.firstSample.x + static_cast<f32>(x0) * d.cellSize,
                           heightOf(low) - terrain.m_skirtDepth,
                           d.firstSample.y + static_cast<f32>(z0) * d.cellSize);
            const Vec3 max(d.firstSample.x + static_cast<f32>(x1) * d.cellSize, heightOf(high),
                           d.firstSample.y + static_cast<f32>(z1) * d.cellSize);
            terrain.m_chunks.push_back({Vec2(static_cast<f32>(x0), static_cast<f32>(z0)), AABB{min, max}});
        }
    }
    terrain.m_bounds = AABB{Vec3(d.firstSample.x, d.minY, d.firstSample.y),
                            Vec3(d.firstSample.x + static_cast<f32>(d.width - 1) * d.cellSize, d.maxY,
                                 d.firstSample.y + static_cast<f32>(d.height - 1) * d.cellSize)};
    return terrain;
}

Result<void> TerrainRenderer::setSurface(Device& device, const TerrainSurfaceDesc& surface)
{
    using Desc = TerrainSurfaceDesc;
    const usize layerCount = surface.layers.size();
    if (layerCount > Desc::kMaxLayers)
    {
        return Error{"terrain surface: " + std::to_string(layerCount) + " layers, at most " +
                     std::to_string(Desc::kMaxLayers)};
    }
    const usize mapsNeeded = (layerCount + 3) / 4;
    if (surface.splatMaps.size() != mapsNeeded)
    {
        return Error{"terrain surface: " + std::to_string(layerCount) + " layers need " +
                     std::to_string(mapsNeeded) + " splat map(s), got " +
                     std::to_string(surface.splatMaps.size())};
    }
    const usize cells = static_cast<usize>(m_desc.width - 1) * (m_desc.height - 1);
    if (!surface.holes.empty() && surface.holes.size() != cells)
    {
        return Error{"terrain surface: hole mask has " + std::to_string(surface.holes.size()) + " bytes, " +
                     std::to_string(m_desc.width - 1) + " x " + std::to_string(m_desc.height - 1) + " = " +
                     std::to_string(cells) + " expected (one per cell)"};
    }
    std::array<f32, Desc::kMaxLayers> tiles{};
    std::vector<const asset::TextureData*> albedos;
    for (usize i = 0; i < layerCount; ++i)
    {
        const Desc::Layer& layer = surface.layers[i];
        if (!(layer.tile > 0.0f))
        {
            return Error{"terrain surface: layer " + std::to_string(i) + " needs a positive tile size"};
        }
        tiles[i] = layer.tile;
        albedos.push_back(layer.albedo);
    }

    // Everything is built first; the previous surface stays if anything fails.
    rhi::Texture splat;
    rhi::Texture layers;
    if (layerCount > 0)
    {
        auto weights = createTextureArray(device, surface.splatMaps, TextureArrayUsage::Data);
        if (!weights)
        {
            return Error{"terrain splat maps: " + weights.error().message};
        }
        auto colours = createTextureArray(device, albedos, TextureArrayUsage::Colour);
        if (!colours)
        {
            return Error{"terrain layers: " + colours.error().message};
        }
        splat = std::move(weights).value();
        layers = std::move(colours).value();
    }
    rhi::Texture holes;
    if (!surface.holes.empty())
    {
        auto mask = device.createTexture({m_desc.width - 1, m_desc.height - 1, rhi::Format::R8, 1});
        if (!mask)
        {
            return mask.error();
        }
        if (auto uploaded = mask.value().upload(0, surface.holes); !uploaded)
        {
            return uploaded.error();
        }
        holes = std::move(mask).value();
    }
    m_splat = std::move(splat);
    m_layers = std::move(layers);
    m_holes = std::move(holes);
    m_tiles = tiles;
    m_layerCount = static_cast<u32>(layerCount);
    m_hasHoles = !surface.holes.empty();
    if (m_layerCount > 0)
    {
        // Pixel centres on samples: uv = 0.5 / W + sample * (W - 1) / (W * (width - 1)).
        const auto w = static_cast<f32>(m_splat.desc().width);
        const auto h = static_cast<f32>(m_splat.desc().height);
        m_splatTransform = Vec4((w - 1.0f) / (w * static_cast<f32>(m_desc.width - 1)),
                                (h - 1.0f) / (h * static_cast<f32>(m_desc.height - 1)), 0.5f / w, 0.5f / h);
    }
    return {};
}

void TerrainRenderer::bindCommon(rhi::ShaderProgram& program, Device& device)
{
    program.setUniform("uSize", Vec2(static_cast<f32>(m_desc.width), static_cast<f32>(m_desc.height)));
    program.setUniform("uFirstSample", m_desc.firstSample);
    program.setUniform("uCellSize", m_desc.cellSize);
    program.setUniform("uHeightRange", Vec2(m_desc.minY, m_desc.maxY));
    program.setUniform("uSkirtDepth", m_skirtDepth);
    device.bindTexture(kHeightUnit, m_heights, m_nearest);
    program.setUniform("uHasHoles", m_hasHoles ? 1 : 0);
    device.bindTexture(kHoleUnit, m_hasHoles ? m_holes : m_noHoles, m_nearest);
}

void TerrainRenderer::drawShadow(Device& device, const Cascade& cascade)
{
    if (m_chunks.empty())
    {
        return;
    }
    const Frustum volume = Frustum::fromViewProjection(cascade.viewProjection);
    device.bindPipeline(m_shadowPipeline);
    bindCommon(*m_shadowProgram, device);
    m_shadowProgram->setUniform("uViewProjection", cascade.viewProjection);
    // Shadow maps are coarse: the lowest detail is enough and keeps the pass cheap.
    const Grid& grid = m_grids[kLodLevels - 1];
    device.bindVertexBuffer(grid.vertices);
    device.bindIndexBuffer(grid.indices, rhi::IndexType::U32);
    for (const Chunk& chunk : m_chunks)
    {
        if (volume.intersects(chunk.bounds))
        {
            m_shadowProgram->setUniform("uChunkOrigin", chunk.origin);
            device.drawIndexed(grid.indexCount);
        }
    }
}

void TerrainRenderer::draw(Device& device, const Camera& camera, const LightList* lights)
{
    m_drawn = 0;
    if (m_chunks.empty())
    {
        return;
    }
    const Frustum view = camera.frustum();
    // Textures before the program: the driver checks the samplers when the program is bound.
    device.bindTexture(kSplatUnit, m_layerCount > 0 ? m_splat : m_noLayers, m_linearClamp);
    device.bindTexture(kLayerUnit, m_layerCount > 0 ? m_layers : m_noLayers, m_linearRepeat);
    device.bindPipeline(m_pipeline);
    bindCommon(*m_program, device);
    m_program->setUniform("uViewProjection", camera.viewProjection());
    m_program->setUniform("uCameraPosition", camera.transform.position);
    m_program->setUniform("uLayerCount", static_cast<i32>(m_layerCount));
    m_program->setUniform("uTiles", std::span<const f32>(m_tiles));
    m_program->setUniform("uSplatTransform", m_splatTransform);
    u32 boundLevel = kLodLevels; // none
    std::array<i32, LightList::kMaxPerObject> indices{};
    for (const Chunk& chunk : m_chunks)
    {
        if (!view.intersects(chunk.bounds))
        {
            continue;
        }
        const Vec3 closest = glm::clamp(camera.transform.position, chunk.bounds.min, chunk.bounds.max);
        const u32 level = lodFor(glm::length(closest - camera.transform.position), lodDistance);
        if (level != boundLevel)
        {
            device.bindVertexBuffer(m_grids[level].vertices);
            device.bindIndexBuffer(m_grids[level].indices, rhi::IndexType::U32);
            boundLevel = level;
        }
        m_selectedLights.clear();
        if (lights != nullptr)
        {
            lights->selectFor(chunk.bounds, m_selectedLights);
        }
        for (usize i = 0; i < m_selectedLights.size(); ++i)
        {
            indices[i] = static_cast<i32>(m_selectedLights[i]);
        }
        m_program->setUniform("uLightIndices", std::span<const i32>(indices));
        m_program->setUniform("uLightCount", static_cast<i32>(m_selectedLights.size()));
        m_program->setUniform("uChunkOrigin", chunk.origin);
        device.drawIndexed(m_grids[level].indexCount);
        ++m_drawn;
    }
}
} // namespace g7::render

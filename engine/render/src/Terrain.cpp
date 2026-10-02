#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/Terrain.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace g7::render
{
namespace
{
constexpr u32 kHeightUnit = 4; // texture unit of the heightmap (0-2 materials, 3 shadow atlas)

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
    auto shadowProgram = shaders.load("terrain_shadow", {"terrain.vert", "shadow.frag", {"SHADOW"}});
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

void TerrainRenderer::bindCommon(rhi::ShaderProgram& program, Device& device)
{
    program.setUniform("uSize", Vec2(static_cast<f32>(m_desc.width), static_cast<f32>(m_desc.height)));
    program.setUniform("uFirstSample", m_desc.firstSample);
    program.setUniform("uCellSize", m_desc.cellSize);
    program.setUniform("uHeightRange", Vec2(m_desc.minY, m_desc.maxY));
    program.setUniform("uSkirtDepth", m_skirtDepth);
    device.bindTexture(kHeightUnit, m_heights, m_nearest);
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
    device.bindPipeline(m_pipeline);
    bindCommon(*m_program, device);
    m_program->setUniform("uViewProjection", camera.viewProjection());
    m_program->setUniform("uCameraPosition", camera.transform.position);
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

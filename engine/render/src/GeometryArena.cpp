#include <g7/render/Device.hpp>
#include <g7/render/GeometryArena.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace g7::render
{
GeometrySlice::GeometrySlice(GeometrySlice&& other) noexcept
    : m_arena(std::exchange(other.m_arena, nullptr)), m_block(other.m_block),
      m_firstVertex(other.m_firstVertex), m_vertexCount(other.m_vertexCount),
      m_firstIndex(other.m_firstIndex), m_indexCount(other.m_indexCount)
{
}

GeometrySlice& GeometrySlice::operator=(GeometrySlice&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_arena = std::exchange(other.m_arena, nullptr);
        m_block = other.m_block;
        m_firstVertex = other.m_firstVertex;
        m_vertexCount = other.m_vertexCount;
        m_firstIndex = other.m_firstIndex;
        m_indexCount = other.m_indexCount;
    }
    return *this;
}

GeometrySlice::~GeometrySlice()
{
    release();
}

void GeometrySlice::release() noexcept
{
    if (m_arena != nullptr)
    {
        m_arena->release(*this);
        m_arena = nullptr;
    }
}

bool GeometryArena::FreeList::take(u32 count, u32& offset)
{
    for (auto it = ranges.begin(); it != ranges.end(); ++it)
    {
        if (it->second >= count)
        {
            offset = it->first;
            const u32 rest = it->second - count;
            ranges.erase(it);
            if (rest > 0)
            {
                ranges.emplace(offset + count, rest);
            }
            return true;
        }
    }
    return false;
}

void GeometryArena::FreeList::give(u32 offset, u32 count)
{
    auto [it, added] = ranges.emplace(offset, count);
    G7_ASSERT(added, "geometry range given back twice");
    // Merge with the following and the preceding range.
    if (const auto next = std::next(it); next != ranges.end() && it->first + it->second == next->first)
    {
        it->second += next->second;
        ranges.erase(next);
    }
    if (it != ranges.begin())
    {
        if (const auto prev = std::prev(it); prev->first + prev->second == it->first)
        {
            prev->second += it->second;
            ranges.erase(it);
        }
    }
}

Result<GeometrySlice> GeometryArena::allocate(Device& device, std::span<const asset::Vertex> vertices,
                                              std::span<const u32> indices)
{
    if (vertices.empty() || indices.empty())
    {
        return Error{"mesh has no geometry"};
    }
    const auto vertexCount = static_cast<u32>(vertices.size());
    const auto indexCount = static_cast<u32>(indices.size());
    GeometrySlice slice;
    bool placed = false;
    for (u32 b = 0; b < m_blocks.size() && !placed; ++b)
    {
        Block& block = m_blocks[b];
        u32 firstVertex = 0;
        u32 firstIndex = 0;
        if (block.freeVertices.take(vertexCount, firstVertex))
        {
            if (block.freeIndices.take(indexCount, firstIndex))
            {
                slice.m_block = b;
                slice.m_firstVertex = firstVertex;
                slice.m_firstIndex = firstIndex;
                placed = true;
            }
            else
            {
                block.freeVertices.give(firstVertex, vertexCount); // indices did not fit: try the next block
            }
        }
    }
    if (!placed)
    {
        // A new block; meshes larger than a standard block get one of their own size.
        const u32 blockVertices = std::max(kBlockVertices, vertexCount);
        const u32 blockIndices = std::max(kBlockIndices, indexCount);
        auto vb = device.createBuffer(
            {static_cast<usize>(blockVertices) * sizeof(asset::Vertex), rhi::BufferUsage::Dynamic, {}});
        auto ib = device.createBuffer(
            {static_cast<usize>(blockIndices) * sizeof(u32), rhi::BufferUsage::Dynamic, {}});
        if (!vb || !ib)
        {
            return Error{"cannot create a geometry block: " +
                         (!vb ? vb.error().message : ib.error().message)};
        }
        Block block;
        block.vertices = std::move(vb).value();
        block.indices = std::move(ib).value();
        if (blockVertices > vertexCount)
        {
            block.freeVertices.ranges.emplace(vertexCount, blockVertices - vertexCount);
        }
        if (blockIndices > indexCount)
        {
            block.freeIndices.ranges.emplace(indexCount, blockIndices - indexCount);
        }
        m_blocks.push_back(std::move(block));
        slice.m_block = static_cast<u32>(m_blocks.size() - 1);
        slice.m_firstVertex = 0;
        slice.m_firstIndex = 0;
    }
    Block& block = m_blocks[slice.m_block];
    const auto vertexBytes =
        std::span(reinterpret_cast<const u8*>(vertices.data()), vertices.size() * sizeof(asset::Vertex));
    const auto indexBytes =
        std::span(reinterpret_cast<const u8*>(indices.data()), indices.size() * sizeof(u32));
    auto vertexUpload =
        block.vertices.update(static_cast<usize>(slice.m_firstVertex) * sizeof(asset::Vertex), vertexBytes);
    auto indexUpload = block.indices.update(static_cast<usize>(slice.m_firstIndex) * sizeof(u32), indexBytes);
    slice.m_arena = this;
    slice.m_vertexCount = vertexCount;
    slice.m_indexCount = indexCount;
    m_usedVertices += vertexCount;
    m_usedIndices += indexCount;
    if (!vertexUpload || !indexUpload)
    {
        return !vertexUpload ? vertexUpload.error() : indexUpload.error(); // the slice gives its ranges back
    }
    return slice;
}

void GeometryArena::release(const GeometrySlice& slice) noexcept
{
    Block& block = m_blocks[slice.m_block];
    block.freeVertices.give(slice.m_firstVertex, slice.m_vertexCount);
    block.freeIndices.give(slice.m_firstIndex, slice.m_indexCount);
    m_usedVertices -= slice.m_vertexCount;
    m_usedIndices -= slice.m_indexCount;
}

void GeometryArena::bind(Device& device, u32 block) const
{
    device.bindVertexBuffer(m_blocks[block].vertices);
    device.bindIndexBuffer(m_blocks[block].indices, rhi::IndexType::U32);
}
} // namespace g7::render

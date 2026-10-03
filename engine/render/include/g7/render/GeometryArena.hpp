#pragma once

// Shared GPU storage for static meshes: large vertex and index blocks that many meshes live in.
// Drawing from a few big buffers instead of one pair per mesh keeps the driver from re-attaching
// thousands of small buffers every frame (M4 visibility work: per-mesh buffers made frame time grow
// with the square of the model count).

#include <g7/asset/MeshData.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <map>
#include <span>
#include <vector>

namespace g7::render
{
class Device;
class GeometryArena;

/// A mesh's place in the arena. Move-only; gives its ranges back when destroyed.
class GeometrySlice
{
public:
    GeometrySlice() = default;
    GeometrySlice(const GeometrySlice&) = delete;
    GeometrySlice& operator=(const GeometrySlice&) = delete;
    GeometrySlice(GeometrySlice&& other) noexcept;
    GeometrySlice& operator=(GeometrySlice&& other) noexcept;
    ~GeometrySlice();

    [[nodiscard]] bool valid() const noexcept { return m_arena != nullptr; }
    [[nodiscard]] u32 block() const noexcept { return m_block; }
    [[nodiscard]] u32 firstVertex() const noexcept { return m_firstVertex; }
    [[nodiscard]] u32 firstIndex() const noexcept { return m_firstIndex; }

private:
    friend class GeometryArena;
    void release() noexcept;

    GeometryArena* m_arena = nullptr;
    u32 m_block = 0;
    u32 m_firstVertex = 0;
    u32 m_vertexCount = 0;
    u32 m_firstIndex = 0;
    u32 m_indexCount = 0;
};

class GeometryArena
{
public:
    static constexpr u32 kBlockVertices = 1u << 20; ///< 48 MB of asset::Vertex per block
    static constexpr u32 kBlockIndices = 1u << 22;  ///< 16 MB of u32 indices per block

    GeometryArena() = default;
    GeometryArena(const GeometryArena&) = delete;
    GeometryArena& operator=(const GeometryArena&) = delete;
    /// Slices point at their arena: it must outlive them and stay where it is (hold it by pointer).
    GeometryArena(GeometryArena&&) = delete;
    GeometryArena& operator=(GeometryArena&&) = delete;

    /// Copies the geometry into the first block with room (first fit), or a new block - one sized
    /// to the mesh if it is larger than a standard block. Indices stay relative to the mesh's first
    /// vertex (drawn with baseVertex).
    [[nodiscard]] Result<GeometrySlice> allocate(Device& device, std::span<const asset::Vertex> vertices,
                                                 std::span<const u32> indices);

    /// Binds the vertex and index buffer of `block`; a pipeline with Mesh::vertexLayout() must be bound.
    void bind(Device& device, u32 block) const;

    [[nodiscard]] usize blockCount() const noexcept { return m_blocks.size(); }
    /// Vertices and indices in use over all blocks.
    [[nodiscard]] usize usedVertices() const noexcept { return m_usedVertices; }
    [[nodiscard]] usize usedIndices() const noexcept { return m_usedIndices; }

private:
    friend class GeometrySlice;

    /// Free ranges (offset -> length), merged with their neighbours when ranges come back.
    struct FreeList
    {
        std::map<u32, u32> ranges;
        [[nodiscard]] bool take(u32 count, u32& offset);
        void give(u32 offset, u32 count);
    };
    struct Block
    {
        rhi::Buffer vertices;
        rhi::Buffer indices;
        FreeList freeVertices;
        FreeList freeIndices;
    };

    void release(const GeometrySlice& slice) noexcept;

    std::vector<Block> m_blocks;
    usize m_usedVertices = 0;
    usize m_usedIndices = 0;
};
} // namespace g7::render

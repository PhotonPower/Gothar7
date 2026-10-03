#pragma once

#include <g7/asset/MeshData.hpp>
#include <g7/core/Geometry.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/GeometryArena.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <span>
#include <vector>

namespace g7::render
{
class Device;

/// Static mesh on the GPU: interleaved vertices (asset::Vertex), u32 indices, submeshes per material.
/// Either in a GeometryArena shared with other meshes (the engine's way: few buffers to bind) or in
/// buffers of its own. Created from asset::MeshData.
class Mesh
{
public:
    Mesh() = default;
    /// Own vertex and index buffer.
    [[nodiscard]] static Result<Mesh> create(Device& device, const asset::MeshData& data);
    /// In `arena`, which must outlive the mesh.
    [[nodiscard]] static Result<Mesh> create(Device& device, GeometryArena& arena,
                                             const asset::MeshData& data);

    /// Attribute locations 0..3: position, normal, uv, tangent (stride 48).
    [[nodiscard]] static std::vector<rhi::VertexAttribute> vertexLayout();
    static constexpr u32 kVertexStride = sizeof(asset::Vertex);

    /// Binds vertex and index buffer (the arena block's, if any); a pipeline with vertexLayout()
    /// must be bound first. Binding the same block again costs nothing (the device caches it).
    void bind(Device& device) const;
    /// Draws one submesh (after bind()).
    void draw(Device& device, usize submesh) const;

    [[nodiscard]] std::span<const asset::Submesh> submeshes() const noexcept { return m_submeshes; }
    /// Arena meshes can be drawn in multi-draw batches (MeshRenderer::drawBatched).
    [[nodiscard]] const GeometryArena* arena() const noexcept { return m_arena; }
    [[nodiscard]] u32 arenaBlock() const noexcept { return m_slice.block(); }
    /// Indices of one submesh as a draw sees them: first index and base vertex include the arena offset.
    [[nodiscard]] DrawIndexedIndirect drawRecord(usize submesh) const noexcept;
    [[nodiscard]] const AABB& bounds() const noexcept { return m_bounds; }

private:
    rhi::Buffer m_vertices;
    rhi::Buffer m_indices;
    const GeometryArena* m_arena = nullptr;
    GeometrySlice m_slice;
    std::vector<asset::Submesh> m_submeshes;
    AABB m_bounds;
};
} // namespace g7::render

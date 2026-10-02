#pragma once

#include <g7/asset/MeshData.hpp>
#include <g7/core/Geometry.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <span>
#include <vector>

namespace g7::render
{
class Device;

/// Static mesh on the GPU: one interleaved vertex buffer (asset::Vertex), one u32 index buffer,
/// submeshes per material. Created from asset::MeshData (glTF now, cooked data from M3).
class Mesh
{
public:
    Mesh() = default;
    [[nodiscard]] static Result<Mesh> create(Device& device, const asset::MeshData& data);

    /// Attribute locations 0..3: position, normal, uv, tangent (stride 48).
    [[nodiscard]] static std::vector<rhi::VertexAttribute> vertexLayout();
    static constexpr u32 kVertexStride = sizeof(asset::Vertex);

    /// Binds vertex and index buffer; a pipeline with vertexLayout() must be bound first.
    void bind(Device& device) const;
    /// Draws one submesh (after bind()).
    void draw(Device& device, usize submesh) const;

    [[nodiscard]] std::span<const asset::Submesh> submeshes() const noexcept { return m_submeshes; }
    [[nodiscard]] const AABB& bounds() const noexcept { return m_bounds; }

private:
    rhi::Buffer m_vertices;
    rhi::Buffer m_indices;
    std::vector<asset::Submesh> m_submeshes;
    AABB m_bounds;
};
} // namespace g7::render

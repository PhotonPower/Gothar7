#include <g7/render/Device.hpp>
#include <g7/render/Mesh.hpp>

#include <cstddef>

namespace g7::render
{
Result<Mesh> Mesh::create(Device& device, const asset::MeshData& data)
{
    if (data.vertices.empty() || data.indices.empty())
    {
        return Error{"mesh has no geometry"};
    }
    const auto vertexBytes = std::span(reinterpret_cast<const u8*>(data.vertices.data()),
                                       data.vertices.size() * sizeof(asset::Vertex));
    const auto indexBytes =
        std::span(reinterpret_cast<const u8*>(data.indices.data()), data.indices.size() * sizeof(u32));

    auto vertices = device.createBuffer({vertexBytes.size(), rhi::BufferUsage::Static, vertexBytes});
    if (!vertices)
    {
        return vertices.error();
    }
    auto indices = device.createBuffer({indexBytes.size(), rhi::BufferUsage::Static, indexBytes});
    if (!indices)
    {
        return indices.error();
    }
    Mesh mesh;
    mesh.m_vertices = std::move(vertices).value();
    mesh.m_indices = std::move(indices).value();
    mesh.m_submeshes = data.submeshes;
    mesh.m_bounds = data.bounds;
    return mesh;
}

Result<Mesh> Mesh::create(Device& device, GeometryArena& arena, const asset::MeshData& data)
{
    auto slice = arena.allocate(device, data.vertices, data.indices);
    if (!slice)
    {
        return slice.error();
    }
    Mesh mesh;
    mesh.m_arena = &arena;
    mesh.m_slice = std::move(slice).value();
    mesh.m_submeshes = data.submeshes;
    mesh.m_bounds = data.bounds;
    return mesh;
}

std::vector<rhi::VertexAttribute> Mesh::vertexLayout()
{
    return {
        {0, rhi::VertexFormat::Float3, static_cast<u32>(offsetof(asset::Vertex, position))},
        {1, rhi::VertexFormat::Float3, static_cast<u32>(offsetof(asset::Vertex, normal))},
        {2, rhi::VertexFormat::Float2, static_cast<u32>(offsetof(asset::Vertex, uv))},
        {3, rhi::VertexFormat::Float4, static_cast<u32>(offsetof(asset::Vertex, tangent))},
    };
}

void Mesh::bind(Device& device) const
{
    if (m_arena != nullptr)
    {
        m_arena->bind(device, m_slice.block());
        return;
    }
    device.bindVertexBuffer(m_vertices);
    device.bindIndexBuffer(m_indices, rhi::IndexType::U32);
}

DrawIndexedIndirect Mesh::drawRecord(usize submesh) const noexcept
{
    const asset::Submesh& range = m_submeshes[submesh];
    return {range.indexCount, 1, m_slice.firstIndex() + range.firstIndex,
            static_cast<i32>(m_slice.firstVertex()), 0};
}

void Mesh::draw(Device& device, usize submesh) const
{
    const asset::Submesh& range = m_submeshes[submesh];
    device.drawIndexed(range.indexCount, m_slice.firstIndex() + range.firstIndex,
                       static_cast<i32>(m_slice.firstVertex()));
}
} // namespace g7::render

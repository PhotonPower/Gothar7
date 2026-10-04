#include <g7/render/Device.hpp>
#include <g7/render/SkinnedMesh.hpp>

#include <algorithm>
#include <cstddef>

namespace g7::render
{
Result<SkinnedMesh> SkinnedMesh::create(Device& device, const asset::SkinnedModelData& model, u32 lod)
{
    SkinnedMesh mesh;
    mesh.m_lod = lod;
    std::vector<SkinnedVertex> vertices;
    std::vector<u32> indices;
    bool first = true;
    for (const asset::SkinnedPartData* part : model.partsForLod(lod))
    {
        const usize base = vertices.size();
        for (usize v = 0; v < part->vertices.size(); ++v)
        {
            SkinnedVertex vertex;
            vertex.base = part->vertices[v];
            std::copy_n(part->joints[v].data(), 4, vertex.joints);
            vertex.weights = part->weights[v];
            vertices.push_back(vertex);
            const Vec3& p = vertex.base.position;
            mesh.m_bounds =
                first ? AABB{p, p} : AABB{glm::min(mesh.m_bounds.min, p), glm::max(mesh.m_bounds.max, p)};
            first = false;
        }
        for (const asset::Submesh& s : part->submeshes)
        {
            mesh.m_submeshes.push_back({static_cast<u32>(indices.size()), s.indexCount, s.material});
            for (u32 i = 0; i < s.indexCount; ++i)
            {
                indices.push_back(part->indices[s.firstIndex + i] + static_cast<u32>(base));
            }
        }
        if (!part->morphs.empty())
        {
            std::vector<usize> identity(part->morphs.size());
            for (usize t = 0; t < identity.size(); ++t)
            {
                identity[t] = t;
            }
            mesh.m_morphParts.push_back({base, part->morphs, std::move(identity)});
            mesh.m_morphCount = std::max(mesh.m_morphCount, part->morphs.size());
        }
    }
    if (vertices.empty() || indices.empty())
    {
        return Error{"skinned mesh: no parts for this LOD"};
    }
    const auto vertexBytes =
        std::span(reinterpret_cast<const u8*>(vertices.data()), vertices.size() * sizeof(SkinnedVertex));
    const auto indexBytes =
        std::span(reinterpret_cast<const u8*>(indices.data()), indices.size() * sizeof(u32));
    const bool morphs = mesh.m_morphCount > 0;
    auto vb = device.createBuffer(
        {vertexBytes.size(), morphs ? rhi::BufferUsage::Dynamic : rhi::BufferUsage::Static, vertexBytes});
    auto ib = device.createBuffer({indexBytes.size(), rhi::BufferUsage::Static, indexBytes});
    if (!vb || !ib)
    {
        return !vb ? vb.error() : ib.error();
    }
    mesh.m_vertices = std::move(vb).value();
    mesh.m_indices = std::move(ib).value();
    if (morphs)
    {
        mesh.m_base = vertices;
        mesh.m_morphed = std::move(vertices);
        mesh.m_weights.assign(mesh.m_morphCount, 0.0f);
    }
    return mesh;
}

std::vector<rhi::VertexAttribute> SkinnedMesh::vertexLayout()
{
    return {
        {0, rhi::VertexFormat::Float3,
         static_cast<u32>(offsetof(SkinnedVertex, base) + offsetof(asset::Vertex, position))},
        {1, rhi::VertexFormat::Float3,
         static_cast<u32>(offsetof(SkinnedVertex, base) + offsetof(asset::Vertex, normal))},
        {2, rhi::VertexFormat::Float2,
         static_cast<u32>(offsetof(SkinnedVertex, base) + offsetof(asset::Vertex, uv))},
        {3, rhi::VertexFormat::Float4,
         static_cast<u32>(offsetof(SkinnedVertex, base) + offsetof(asset::Vertex, tangent))},
        {5, rhi::VertexFormat::UInt16x4, static_cast<u32>(offsetof(SkinnedVertex, joints))},
        {6, rhi::VertexFormat::Float4, static_cast<u32>(offsetof(SkinnedVertex, weights))},
    };
}

void SkinnedMesh::bind(Device& device) const
{
    device.bindVertexBuffer(m_vertices);
    device.bindIndexBuffer(m_indices, rhi::IndexType::U32);
}

void SkinnedMesh::draw(Device& device, usize submesh) const
{
    const asset::Submesh& range = m_submeshes[submesh];
    device.drawIndexed(range.indexCount, range.firstIndex, 0);
}

void SkinnedMesh::mapMorphNames(std::span<const std::string_view> names)
{
    if (m_morphParts.empty())
    {
        return; // LOD 1/2: no targets
    }
    for (MorphPart& part : m_morphParts)
    {
        for (usize t = 0; t < part.targets.size(); ++t)
        {
            if (part.targets[t].name.empty())
            {
                continue;
            }
            const auto it = std::find(names.begin(), names.end(), part.targets[t].name);
            part.weightIndex[t] = it != names.end() ? static_cast<usize>(it - names.begin()) : ~usize(0);
        }
    }
    m_morphCount = std::max(m_morphCount, names.size());
    m_weights.resize(m_morphCount, 0.0f);
}

void SkinnedMesh::setMorphWeights(std::span<const f32> weights)
{
    if (m_morphCount == 0)
    {
        return; // LOD 1/2: faces have no targets there (characters-pipeline.md §6.1)
    }
    bool changed = false;
    for (usize t = 0; t < m_morphCount; ++t)
    {
        const f32 w = t < weights.size() ? weights[t] : 0.0f;
        changed = changed || w != m_weights[t];
        m_weights[t] = w;
    }
    if (!changed)
    {
        return;
    }
    for (const MorphPart& part : m_morphParts)
    {
        const usize count = part.targets.front().positions.size();
        usize lowest = part.firstVertex + count;
        usize highest = part.firstVertex;
        for (usize v = 0; v < count; ++v)
        {
            SkinnedVertex& out = m_morphed[part.firstVertex + v];
            const SkinnedVertex& base = m_base[part.firstVertex + v];
            Vec3 position = base.base.position;
            Vec3 normal = base.base.normal;
            for (usize t = 0; t < part.targets.size(); ++t)
            {
                const usize w = part.weightIndex[t];
                if (w >= m_weights.size() || m_weights[w] == 0.0f)
                {
                    continue;
                }
                position += part.targets[t].positions[v] * m_weights[w];
                if (!part.targets[t].normals.empty())
                {
                    normal += part.targets[t].normals[v] * m_weights[w];
                }
            }
            if (position != out.base.position || normal != out.base.normal)
            {
                out.base.position = position;
                out.base.normal = glm::length(normal) > 1e-6f ? glm::normalize(normal) : base.base.normal;
                lowest = std::min(lowest, part.firstVertex + v);
                highest = std::max(highest, part.firstVertex + v + 1);
            }
        }
        if (lowest < highest)
        {
            (void)m_vertices.update(lowest * sizeof(SkinnedVertex),
                                    std::span(reinterpret_cast<const u8*>(m_morphed.data() + lowest),
                                              (highest - lowest) * sizeof(SkinnedVertex)));
        }
    }
}
} // namespace g7::render

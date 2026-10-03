#pragma once

// Skinned meshes on the GPU (M6 part C): one LOD level of a figure (all its parts) with bone indices and
// weights per vertex; the bones move it in the vertex shader (SKINNED shader variants, MeshRenderer::
// drawSkinned). Morph targets (faces, head_lod0 only - characters-pipeline.md §6.1) are applied on the CPU
// and uploaded when their weights change.

#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/Geometry.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <span>
#include <vector>

namespace g7::render
{
class Device;

/// asset::Vertex plus four bone indices and weights (72 bytes).
struct SkinnedVertex
{
    asset::Vertex base;
    u16 joints[4] = {0, 0, 0, 0};
    Vec4 weights{1.0f, 0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(SkinnedVertex) == 72);

class SkinnedMesh
{
public:
    SkinnedMesh() = default;
    /// The parts of LOD `lod` (SkinnedModelData::partsForLod), merged into one vertex and index buffer.
    [[nodiscard]] static Result<SkinnedMesh> create(Device& device, const asset::SkinnedModelData& model,
                                                    u32 lod);

    /// Attribute locations 0..3 as Mesh, 5: bone indices (uvec4), 6: weights.
    [[nodiscard]] static std::vector<rhi::VertexAttribute> vertexLayout();
    static constexpr u32 kVertexStride = sizeof(SkinnedVertex);

    void bind(Device& device) const;
    void draw(Device& device, usize submesh) const;

    [[nodiscard]] std::span<const asset::Submesh> submeshes() const noexcept { return m_submeshes; }
    [[nodiscard]] const AABB& bounds() const noexcept { return m_bounds; }
    [[nodiscard]] u32 lod() const noexcept { return m_lod; }

    /// Number of morph targets (the most a part of this LOD has; 0 below LOD 0).
    [[nodiscard]] usize morphCount() const noexcept { return m_morphCount; }
    /// Morph weights (missing ones are 0); uploads the affected vertices if anything changed.
    void setMorphWeights(std::span<const f32> weights);

private:
    struct MorphPart
    {
        usize firstVertex = 0;
        std::vector<asset::MorphTargetData> targets;
    };
    rhi::Buffer m_vertices;
    rhi::Buffer m_indices;
    std::vector<asset::Submesh> m_submeshes;
    AABB m_bounds;
    u32 m_lod = 0;
    std::vector<SkinnedVertex> m_base; ///< kept only when there are morph targets
    std::vector<SkinnedVertex> m_morphed;
    std::vector<MorphPart> m_morphParts;
    std::vector<f32> m_weights;
    usize m_morphCount = 0;
};
} // namespace g7::render

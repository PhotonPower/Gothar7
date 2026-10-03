#pragma once

#include <g7/asset/MeshData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Shadows.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace g7::render
{
class SkinnedMesh;
class Device;
class Mesh;
class ShaderLibrary;
struct Camera;

/// GPU material (render.md: deliberately simple – base colour, normal map, emissive, alpha test).
/// Texture pointers refer into the owning MaterialSet and are never null (fallbacks).
struct Material
{
    const rhi::Texture* baseColor = nullptr; ///< sRGB
    const rhi::Texture* normal = nullptr;    ///< linear
    const rhi::Texture* emissive = nullptr;  ///< sRGB
    Vec4 baseColorFactor{1.0f};
    Vec3 emissiveFactor{0.0f};
    f32 normalScale = 1.0f;
    bool normalTwoChannel = false; ///< BC5 normal map: the shader reconstructs Z
    f32 alphaCutoff = 0.5f;
    asset::AlphaMode alphaMode = asset::AlphaMode::Opaque;
    bool doubleSided = false;
};

/// Neutral 1x1 textures shared by every MaterialSet of a renderer: materials without images then use
/// the same textures in every model, so equal materials of different models batch together.
struct MaterialDefaults
{
    rhi::Texture white;      ///< sRGB white (base colour, emissive)
    rhi::Texture flatNormal; ///< linear (0.5, 0.5, 1)
};

/// The textures and materials of one model. Images are uploaded once per use (sRGB for colour,
/// linear for normals); missing or broken images fall back to neutral 1x1 textures with a warning.
class MaterialSet
{
public:
    MaterialSet() = default;
    /// Supplies the decoded image for an external `ImageSource` (uri set), or nullptr if it is
    /// missing (the material then uses a neutral fallback). Embedded images are decoded here.
    using ImageLookup = std::function<const asset::TextureData*(const asset::ImageSource&)>;

    /// With `defaults` (MeshRenderer::defaults()) materials without an image use the shared neutral
    /// textures; the set keeps them alive. Without, the set makes its own.
    [[nodiscard]] static Result<MaterialSet>
    create(Device& device, const asset::MeshData& mesh, const ImageLookup& lookup,
           std::shared_ptr<const MaterialDefaults> defaults = nullptr);
    /// Convenience for tools and tests: external image URIs are files relative to `modelDirectory`.
    [[nodiscard]] static Result<MaterialSet> create(Device& device, const asset::MeshData& mesh,
                                                    const fs::Path& modelDirectory);

    [[nodiscard]] const Material& operator[](usize index) const { return m_materials[index]; }
    [[nodiscard]] usize size() const noexcept { return m_materials.size(); }

private:
    std::vector<rhi::Texture> m_textures; // owns every texture, fallbacks included (stable on move)
    std::vector<Material> m_materials;
    std::shared_ptr<const MaterialDefaults> m_defaults;
};

/// Shadows of the current frame, handed to MeshRenderer::setLighting.
struct ShadowFrame
{
    const ShadowMap* map = nullptr;
    std::span<const Cascade> cascades;
    const Camera* camera = nullptr; ///< the camera the cascades were computed for
    bool debugColours = false;
};

/// Draws meshes with their materials: opaque and alpha-tested submeshes first, then translucent
/// ones (alpha blend, no depth writes; not yet sorted – that comes with the render scene).
/// One object for MeshRenderer::drawBatched / drawShadowBatched.
struct MeshDrawItem
{
    const Mesh* mesh = nullptr;
    const MaterialSet* materials = nullptr;
    Mat4 model{1.0f};
    AABB bounds; ///< world bounds (point light selection)
};

/// What the last batched pass did (statistics, tests).
struct BatchStats
{
    u32 groups = 0;       ///< multi-draw calls (one per pipeline, material and geometry block)
    u32 batchedDraws = 0; ///< submeshes drawn through them
    u32 singleDraws = 0;  ///< submeshes drawn one by one (translucent, meshes outside an arena)
};

class MeshRenderer
{
public:
    MeshRenderer() = default;
    /// Loads "mesh" and "mesh_alpha_test" from the library; `anisotropy` for the material sampler.
    /// Also loads "shadow" and "shadow_alpha_test" for the shadow pass (depth bias from `shadows`).
    [[nodiscard]] static Result<MeshRenderer> create(Device& device, ShaderLibrary& shaders, f32 anisotropy,
                                                     const ShadowSettings& shadows = {});

    /// Uploads the frame's lighting (environment + all point lights). Call once per frame before
    /// draw(); `lights` must stay alive until the frame's draws are done.
    void setLighting(Device& device, const Environment& environment, const LightList& lights,
                     const ShadowFrame* shadows = nullptr);

    /// Renders a mesh's depth into the current shadow cascade (after ShadowMap::beginCascade).
    /// Translucent submeshes cast no shadow; alpha-tested ones cast holed shadows.
    void drawShadow(Device& device, const Mesh& mesh, const MaterialSet& materials, const Mat4& model,
                    const Cascade& cascade);

    /// Binds the lighting block (binding 0) and the shadow atlas (unit 3) of the last setLighting(),
    /// for other renderers that use common/lighting.glsl (terrain).
    void bindLighting(Device& device) const;

    /// Draws with the lighting set by setLighting(); each object gets the (at most 8) point lights
    /// that reach its world bounds.
    void draw(Device& device, const Mesh& mesh, const MaterialSet& materials, const Mat4& model,
              const Camera& camera, i32 onlySubmesh = -1); ///< -1: all submeshes

    /// A skinned mesh (M6): `bones` are the skinning matrices (model-space bone * inverse bind), at most
    /// asset::kMaxBones. Same lighting and materials as draw().
    void drawSkinned(Device& device, const SkinnedMesh& mesh, const MaterialSet& materials, const Mat4& model,
                     std::span<const Mat4> bones, const Camera& camera);
    /// drawShadow() for a skinned mesh.
    void drawShadowSkinned(Device& device, const SkinnedMesh& mesh, const MaterialSet& materials,
                           const Mat4& model, std::span<const Mat4> bones, const Cascade& cascade);

    /// Like draw() for many objects: opaque and alpha-tested submeshes of arena meshes are grouped by
    /// pipeline, material values and geometry block and drawn with one multi-draw call per group; the
    /// rest (translucent submeshes, meshes outside an arena) one by one afterwards. Same image as draw().
    void drawBatched(Device& device, std::span<const MeshDrawItem> items, const Camera& camera);
    /// drawShadow() for many objects, grouped the same way.
    void drawShadowBatched(Device& device, std::span<const MeshDrawItem> items, const Cascade& cascade);
    [[nodiscard]] const BatchStats& lastBatch() const noexcept { return m_batch; }
    /// Starts a frame for the batched passes (rotates their buffers). Call once per frame before them.
    void beginFrame() noexcept;

    /// Neutral textures to share between MaterialSets (MaterialSet::create), so materials without
    /// images batch across models.
    [[nodiscard]] const std::shared_ptr<const MaterialDefaults>& defaults() const noexcept
    {
        return m_defaults;
    }

private:
    enum Variant : u8
    {
        Opaque,
        AlphaTest,
        Blend,
        VariantCount
    };

    [[nodiscard]] const rhi::Pipeline& pipeline(Variant variant, bool doubleSided) const;
    void drawSubmesh(Device& device, const Mesh& mesh, usize submesh, const Material& material);
    void uploadBones(Device& device, std::span<const Mat4> bones);

    // Skinned meshes (SKINNED shader variants, SkinnedMesh vertex layout, bones in uniform block 2).
    rhi::ShaderProgram* m_skinnedProgram = nullptr;
    rhi::ShaderProgram* m_skinnedAlphaTestProgram = nullptr;
    std::array<rhi::Pipeline, VariantCount * 2> m_skinnedPipelines;
    rhi::ShaderProgram* m_skinnedShadowProgram = nullptr;
    rhi::ShaderProgram* m_skinnedShadowAlphaTestProgram = nullptr;
    std::array<rhi::Pipeline, 2> m_skinnedShadowPipelines;
    rhi::Buffer m_bonesBuffer;

    rhi::ShaderProgram* m_program = nullptr;                 // owned by the ShaderLibrary
    rhi::ShaderProgram* m_alphaTestProgram = nullptr;        // owned by the ShaderLibrary
    std::array<rhi::Pipeline, VariantCount * 2> m_pipelines; // [variant][culled, double-sided]
    rhi::Sampler m_sampler;
    rhi::Buffer m_lightingBuffer; // GpuLighting, uniform block binding 0
    rhi::ShaderProgram* m_shadowProgram = nullptr;
    rhi::ShaderProgram* m_shadowAlphaTestProgram = nullptr;
    std::array<rhi::Pipeline, 2> m_shadowPipelines; // [opaque, alpha test]
    const ShadowMap* m_shadowMap = nullptr;
    const LightList* m_lights = nullptr;
    std::vector<u32> m_selected;

    // Multi-draw: programs with MULTI_DRAW, pipelines with the per-draw index (binding 1, divisor 1).
    /// Grouping key of a batched submesh: what must be equal to share one multi-draw call.
    struct BatchKey
    {
        u64 blockAndFlags = 0; // geometry block << 32 | variant | doubleSided << 8 | twoChannel << 9
        std::array<u64, 3> textures{};
        std::array<u32, 9> values{}; // float bits: base colour (4), emissive (3), normal scale, cutoff
        friend auto operator<=>(const BatchKey&, const BatchKey&) = default;
    };
    struct BatchEntry
    {
        BatchKey key;
        DrawIndexedIndirect command;
        const Mesh* mesh = nullptr;
        const Material* material = nullptr;
    };
    void drawGroups(Device& device, bool shadow, const Mat4& viewProjection, const Vec3& cameraPosition);
    void uploadBatch(Device& device);
    void bindMaterial(rhi::ShaderProgram& program, Device& device, const Material& material);
    rhi::ShaderProgram* m_multiProgram = nullptr;
    rhi::ShaderProgram* m_multiAlphaTestProgram = nullptr;
    std::array<rhi::Pipeline, VariantCount * 2> m_multiPipelines;
    rhi::ShaderProgram* m_multiShadowProgram = nullptr;
    rhi::ShaderProgram* m_multiShadowAlphaTestProgram = nullptr;
    std::array<rhi::Pipeline, 2> m_multiShadowPipelines;
    // Per frame one of three buffer sets; every pass of the frame appends behind the previous one, so
    // no buffer is rewritten while the GPU may still read it (drivers would stall on that).
    struct BatchBuffers
    {
        rhi::Buffer draws;    // DrawData per batched draw (storage block 0)
        rhi::Buffer commands; // DrawIndexedIndirect per batched draw
        rhi::Buffer indices;  // 0, 1, 2 ... as per-instance draw index
        usize capacity = 0;   // records
    };
    std::array<BatchBuffers, 3> m_batchBuffers;
    u32 m_batchFrame = 0;
    usize m_frameRecords = 0; // records used by earlier passes of this frame
    usize m_batchBase = 0;    // first record of the current pass
    std::vector<BatchEntry> m_entries;
    std::vector<u8> m_drawData;
    std::vector<DrawIndexedIndirect> m_commands;
    std::vector<std::pair<const MeshDrawItem*, usize>> m_singles; // (item, submesh)
    BatchStats m_batch;
    std::shared_ptr<const MaterialDefaults> m_defaults;
};
} // namespace g7::render

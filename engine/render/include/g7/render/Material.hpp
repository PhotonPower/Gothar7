#pragma once

#include <g7/asset/MeshData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Shadows.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <array>
#include <functional>
#include <vector>

namespace g7::render
{
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

/// The textures and materials of one model. Images are uploaded once per use (sRGB for colour,
/// linear for normals); missing or broken images fall back to neutral 1x1 textures with a warning.
class MaterialSet
{
public:
    MaterialSet() = default;
    /// Supplies the decoded image for an external `ImageSource` (uri set), or nullptr if it is
    /// missing (the material then uses a neutral fallback). Embedded images are decoded here.
    using ImageLookup = std::function<const asset::TextureData*(const asset::ImageSource&)>;

    [[nodiscard]] static Result<MaterialSet> create(Device& device, const asset::MeshData& mesh,
                                                    const ImageLookup& lookup);
    /// Convenience for tools and tests: external image URIs are files relative to `modelDirectory`.
    [[nodiscard]] static Result<MaterialSet> create(Device& device, const asset::MeshData& mesh,
                                                    const fs::Path& modelDirectory);

    [[nodiscard]] const Material& operator[](usize index) const { return m_materials[index]; }
    [[nodiscard]] usize size() const noexcept { return m_materials.size(); }

private:
    std::vector<rhi::Texture> m_textures; // owns every texture, fallbacks included (stable on move)
    std::vector<Material> m_materials;
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
              const Camera& camera);

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
};
} // namespace g7::render

#include <g7/asset/ImageData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/SkinnedMesh.hpp>
#include <g7/render/TextureUpload.hpp>

#include <map>
#include <string>
#include <utility>

namespace g7::render
{
namespace
{
/// A texture file for the path-based convenience: KTX2 (cooked) or PNG/JPEG/TGA/BMP.
Result<asset::TextureData> loadTextureFile(const fs::Path& path)
{
    if (equalsIgnoreCase(fs::toUtf8(path.extension()), ".ktx2"))
    {
        auto bytes = fs::readFile(path);
        if (!bytes)
        {
            return bytes.error();
        }
        return asset::decodeKtx2(bytes.value(), fs::toUtf8(path));
    }
    auto image = asset::loadImage(path);
    if (!image)
    {
        return image.error();
    }
    return asset::textureFromImage(std::move(image).value());
}
} // namespace

Result<MaterialSet> MaterialSet::create(Device& device, const asset::MeshData& mesh,
                                        const fs::Path& modelDirectory)
{
    // Decoded files stay alive until the set is built (the lookup hands out pointers).
    std::map<std::string, asset::TextureData, std::less<>> files;
    return create(device, mesh,
                  [&](const asset::ImageSource& source) -> const asset::TextureData*
                  {
                      if (const auto it = files.find(source.uri); it != files.end())
                      {
                          return &it->second;
                      }
                      auto texture = loadTextureFile(modelDirectory / fs::fromUtf8(source.uri));
                      if (!texture)
                      {
                          G7_LOG_WARN("render", "texture skipped: {}", texture.error().message);
                          return nullptr;
                      }
                      return &files.emplace(source.uri, std::move(texture).value()).first->second;
                  });
}

Result<MaterialSet> MaterialSet::create(Device& device, const asset::MeshData& mesh,
                                        const ImageLookup& lookup,
                                        std::shared_ptr<const MaterialDefaults> defaults, TextureCache* cache)
{
    MaterialSet set;
    set.m_defaults = std::move(defaults);
    // Every texture is held by a shared_ptr (own, cached or the shared defaults), so the pointers in
    // the materials stay valid when the set moves.
    const auto own = [&](Result<rhi::Texture> texture) -> Result<const rhi::Texture*>
    {
        if (!texture)
        {
            return texture.error();
        }
        set.m_textures.push_back(std::make_shared<const rhi::Texture>(std::move(texture).value()));
        return set.m_textures.back().get();
    };
    auto white = set.m_defaults ? Result<const rhi::Texture*>(&set.m_defaults->white)
                                : own(createSolidTexture(device, 255, 255, 255, 255, true));
    auto flatNormal = set.m_defaults ? Result<const rhi::Texture*>(&set.m_defaults->flatNormal)
                                     : own(createSolidTexture(device, 128, 128, 255, 255, false));
    if (!white || !flatNormal)
    {
        return Error{"cannot create fallback textures"};
    }

    std::map<std::pair<i32, bool>, const rhi::Texture*> uploaded; // (image, sRGB) -> texture
    const auto textureFor = [&](i32 image, bool srgb, const rhi::Texture* fallback) -> const rhi::Texture*
    {
        if (image < 0 || static_cast<usize>(image) >= mesh.images.size())
        {
            return fallback;
        }
        const auto key = std::make_pair(image, srgb);
        if (const auto it = uploaded.find(key); it != uploaded.end())
        {
            return it->second;
        }
        const asset::ImageSource& source = mesh.images[static_cast<usize>(image)];
        Result<const rhi::Texture*> texture = Error{"image '" + source.uri + "' not available"};
        if (!source.encoded.empty())
        {
            auto decoded = asset::decodeImage(source.encoded, "embedded image");
            texture = decoded ? own(createTexture(device, decoded.value(), {srgb, true}))
                              : Result<const rhi::Texture*>(decoded.error());
        }
        else if (const ExternalImage external = lookup ? lookup(source) : ExternalImage{};
                 external.data != nullptr)
        {
            if (cache != nullptr && !external.cacheKey.empty())
            {
                auto shared = cache->get(device, external.cacheKey, external.version, *external.data, srgb);
                if (shared)
                {
                    set.m_textures.push_back(shared.value());
                    texture = shared.value().get();
                }
                else
                {
                    texture = shared.error();
                }
            }
            else
            {
                texture = own(createTexture(device, *external.data, srgb));
            }
        }
        const rhi::Texture* result = fallback;
        if (texture)
        {
            result = texture.value();
        }
        else
        {
            G7_LOG_DEBUG("render", "texture fallback: {}", texture.error().message);
        }
        uploaded.emplace(key, result);
        return result;
    };

    for (const asset::MaterialInfo& info : mesh.materials)
    {
        Material material;
        material.baseColor = textureFor(info.baseColorImage, true, white.value());
        material.normal = textureFor(info.normalImage, false, flatNormal.value());
        material.emissive = textureFor(info.emissiveImage, true, white.value());
        material.baseColorFactor = info.baseColor;
        material.emissiveFactor = info.emissive;
        material.normalScale = info.normalScale;
        material.normalTwoChannel = material.normal->desc().format == rhi::Format::BC5;
        material.alphaCutoff = info.alphaCutoff;
        material.alphaMode = info.alphaMode;
        material.doubleSided = info.doubleSided;
        set.m_materials.push_back(material);
    }
    return set;
}

Result<MeshRenderer> MeshRenderer::create(Device& device, ShaderLibrary& shaders, f32 anisotropy,
                                          const ShadowSettings& shadows)
{
    MeshRenderer renderer;
    auto program = shaders.load("mesh", {"mesh.vert", "mesh.frag", {}});
    if (!program)
    {
        return program.error();
    }
    auto alphaTest = shaders.load("mesh_alpha_test", {"mesh.vert", "mesh.frag", {"ALPHA_TEST"}});
    if (!alphaTest)
    {
        return alphaTest.error();
    }
    renderer.m_program = program.value();
    renderer.m_alphaTestProgram = alphaTest.value();

    for (u32 variant = 0; variant < VariantCount; ++variant)
    {
        for (u32 doubleSided = 0; doubleSided < 2; ++doubleSided)
        {
            rhi::PipelineDesc desc;
            desc.program = variant == AlphaTest ? renderer.m_alphaTestProgram : renderer.m_program;
            desc.attributes = Mesh::vertexLayout();
            desc.vertexStride = Mesh::kVertexStride;
            desc.cull = doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
            if (variant == Blend)
            {
                desc.blend = rhi::BlendMode::Alpha;
                desc.depthWrite = false; // translucent surfaces must not hide what is behind them
            }
            auto pipeline = device.createPipeline(desc);
            if (!pipeline)
            {
                return pipeline.error();
            }
            renderer.m_pipelines[variant * 2 + doubleSided] = std::move(pipeline).value();
        }
    }

    rhi::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy = anisotropy;
    auto sampler = device.createSampler(samplerDesc);
    if (!sampler)
    {
        return sampler.error();
    }
    renderer.m_sampler = std::move(sampler).value();

    auto shadow = shaders.load("shadow", {"shadow.vert", "shadow.frag", {}});
    auto shadowAlpha = shaders.load("shadow_alpha_test", {"shadow.vert", "shadow.frag", {"ALPHA_TEST"}});
    if (!shadow || !shadowAlpha)
    {
        return !shadow ? shadow.error() : shadowAlpha.error();
    }
    renderer.m_shadowProgram = shadow.value();
    renderer.m_shadowAlphaTestProgram = shadowAlpha.value();
    for (usize i = 0; i < renderer.m_shadowPipelines.size(); ++i)
    {
        rhi::PipelineDesc desc;
        desc.program = i == 0 ? renderer.m_shadowProgram : renderer.m_shadowAlphaTestProgram;
        desc.attributes = Mesh::vertexLayout();
        desc.vertexStride = Mesh::kVertexStride;
        desc.cull = rhi::CullMode::None;               // thin and double-sided geometry must still cast
        desc.depthCompare = rhi::CompareOp::LessEqual; // shadow depth is not reversed
        desc.depthBias = {shadows.depthBias, shadows.slopeBias};
        auto pipeline = device.createPipeline(desc);
        if (!pipeline)
        {
            return pipeline.error();
        }
        renderer.m_shadowPipelines[i] = std::move(pipeline).value();
    }

    // Multi-draw variants: the draw index as per-instance attribute 4 (binding 1, divisor 1).
    auto multi = shaders.load("mesh_multi", {"mesh.vert", "mesh.frag", {"MULTI_DRAW"}});
    auto multiAlpha =
        shaders.load("mesh_alpha_test_multi", {"mesh.vert", "mesh.frag", {"ALPHA_TEST", "MULTI_DRAW"}});
    auto multiShadow = shaders.load("shadow_multi", {"shadow.vert", "shadow.frag", {"MULTI_DRAW"}});
    auto multiShadowAlpha =
        shaders.load("shadow_alpha_test_multi", {"shadow.vert", "shadow.frag", {"ALPHA_TEST", "MULTI_DRAW"}});
    if (!multi || !multiAlpha || !multiShadow || !multiShadowAlpha)
    {
        return !multi         ? multi.error()
               : !multiAlpha  ? multiAlpha.error()
               : !multiShadow ? multiShadow.error()
                              : multiShadowAlpha.error();
    }
    renderer.m_multiProgram = multi.value();
    renderer.m_multiAlphaTestProgram = multiAlpha.value();
    renderer.m_multiShadowProgram = multiShadow.value();
    renderer.m_multiShadowAlphaTestProgram = multiShadowAlpha.value();
    std::vector<rhi::VertexAttribute> multiLayout = Mesh::vertexLayout();
    multiLayout.push_back({4, rhi::VertexFormat::UInt1, 0, 1});
    for (u32 variant : {u32(Opaque), u32(AlphaTest)}) // translucent submeshes are never batched
    {
        for (u32 doubleSided = 0; doubleSided < 2; ++doubleSided)
        {
            rhi::PipelineDesc desc;
            desc.program = variant == AlphaTest ? renderer.m_multiAlphaTestProgram : renderer.m_multiProgram;
            desc.attributes = multiLayout;
            desc.vertexStride = Mesh::kVertexStride;
            desc.instanceStride = sizeof(u32);
            desc.cull = doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
            auto pipeline = device.createPipeline(desc);
            if (!pipeline)
            {
                return pipeline.error();
            }
            renderer.m_multiPipelines[variant * 2 + doubleSided] = std::move(pipeline).value();
        }
    }
    for (usize i = 0; i < renderer.m_multiShadowPipelines.size(); ++i)
    {
        rhi::PipelineDesc desc;
        desc.program = i == 0 ? renderer.m_multiShadowProgram : renderer.m_multiShadowAlphaTestProgram;
        desc.attributes = multiLayout;
        desc.vertexStride = Mesh::kVertexStride;
        desc.instanceStride = sizeof(u32);
        desc.cull = rhi::CullMode::None;
        desc.depthCompare = rhi::CompareOp::LessEqual;
        desc.depthBias = {shadows.depthBias, shadows.slopeBias};
        auto pipeline = device.createPipeline(desc);
        if (!pipeline)
        {
            return pipeline.error();
        }
        renderer.m_multiShadowPipelines[i] = std::move(pipeline).value();
    }

    // Skinned variants (M6): the same shaders with SKINNED, the SkinnedMesh layout, bones in block 2.
    auto skinned = shaders.load("mesh_skinned", {"mesh.vert", "mesh.frag", {"SKINNED"}});
    auto skinnedAlpha =
        shaders.load("mesh_alpha_test_skinned", {"mesh.vert", "mesh.frag", {"ALPHA_TEST", "SKINNED"}});
    auto skinnedShadow = shaders.load("shadow_skinned", {"shadow.vert", "shadow.frag", {"SKINNED"}});
    auto skinnedShadowAlpha =
        shaders.load("shadow_alpha_test_skinned", {"shadow.vert", "shadow.frag", {"ALPHA_TEST", "SKINNED"}});
    if (!skinned || !skinnedAlpha || !skinnedShadow || !skinnedShadowAlpha)
    {
        return !skinned         ? skinned.error()
               : !skinnedAlpha  ? skinnedAlpha.error()
               : !skinnedShadow ? skinnedShadow.error()
                                : skinnedShadowAlpha.error();
    }
    renderer.m_skinnedProgram = skinned.value();
    renderer.m_skinnedAlphaTestProgram = skinnedAlpha.value();
    renderer.m_skinnedShadowProgram = skinnedShadow.value();
    renderer.m_skinnedShadowAlphaTestProgram = skinnedShadowAlpha.value();
    for (u32 variant = 0; variant < VariantCount; ++variant)
    {
        for (u32 doubleSided = 0; doubleSided < 2; ++doubleSided)
        {
            rhi::PipelineDesc desc;
            desc.program =
                variant == AlphaTest ? renderer.m_skinnedAlphaTestProgram : renderer.m_skinnedProgram;
            desc.attributes = SkinnedMesh::vertexLayout();
            desc.vertexStride = SkinnedMesh::kVertexStride;
            desc.cull = doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
            if (variant == Blend)
            {
                desc.blend = rhi::BlendMode::Alpha;
                desc.depthWrite = false;
            }
            auto pipeline = device.createPipeline(desc);
            if (!pipeline)
            {
                return pipeline.error();
            }
            renderer.m_skinnedPipelines[variant * 2 + doubleSided] = std::move(pipeline).value();
        }
    }
    for (usize i = 0; i < renderer.m_skinnedShadowPipelines.size(); ++i)
    {
        rhi::PipelineDesc desc;
        desc.program = i == 0 ? renderer.m_skinnedShadowProgram : renderer.m_skinnedShadowAlphaTestProgram;
        desc.attributes = SkinnedMesh::vertexLayout();
        desc.vertexStride = SkinnedMesh::kVertexStride;
        desc.cull = rhi::CullMode::None;
        desc.depthCompare = rhi::CompareOp::LessEqual;
        desc.depthBias = {shadows.depthBias, shadows.slopeBias};
        auto pipeline = device.createPipeline(desc);
        if (!pipeline)
        {
            return pipeline.error();
        }
        renderer.m_skinnedShadowPipelines[i] = std::move(pipeline).value();
    }
    auto bones = device.createBuffer({asset::kMaxBones * sizeof(Mat4), rhi::BufferUsage::Dynamic, {}});
    if (!bones)
    {
        return bones.error();
    }
    renderer.m_bonesBuffer = std::move(bones).value();

    // Neutral textures shared by the MaterialSets made with defaults().
    auto white = createSolidTexture(device, 255, 255, 255, 255, true);
    auto flatNormal = createSolidTexture(device, 128, 128, 255, 255, false);
    if (!white || !flatNormal)
    {
        return Error{"cannot create the neutral textures"};
    }
    renderer.m_defaults = std::make_shared<const MaterialDefaults>(
        MaterialDefaults{std::move(white).value(), std::move(flatNormal).value()});

    auto lighting = device.createBuffer({sizeof(GpuLighting), rhi::BufferUsage::Dynamic, {}});
    if (!lighting)
    {
        return lighting.error();
    }
    renderer.m_lightingBuffer = std::move(lighting).value();
    // Neutral default until setLighting(): ambient white, no sun, no point lights.
    renderer.setLighting(
        device, Environment{.sunIntensity = 0.0f, .ambientSky = Vec3(1.0f), .ambientGround = Vec3(1.0f)},
        LightList{});
    renderer.m_lights = nullptr;
    return renderer;
}

void MeshRenderer::setLighting(Device&, const Environment& environment, const LightList& lights,
                               const ShadowFrame* shadows)
{
    GpuLighting gpu = packLighting(environment, lights);
    m_shadowMap = nullptr;
    if (shadows && shadows->map && shadows->camera && !shadows->cascades.empty())
    {
        packShadows(gpu, shadows->cascades, shadows->map->settings(), *shadows->camera,
                    shadows->debugColours);
        m_shadowMap = shadows->map;
    }
    // Only the used part of the point arrays changes; the block is small enough to upload whole.
    (void)m_lightingBuffer.update(0, std::span(reinterpret_cast<const u8*>(&gpu), sizeof(gpu)));
    m_lights = &lights;
}

const rhi::Pipeline& MeshRenderer::pipeline(Variant variant, bool doubleSided) const
{
    return m_pipelines[static_cast<usize>(variant) * 2 + (doubleSided ? 1 : 0)];
}

void MeshRenderer::drawSubmesh(Device& device, const Mesh& mesh, usize submesh, const Material& material)
{
    const Variant variant = material.alphaMode == asset::AlphaMode::Mask    ? AlphaTest
                            : material.alphaMode == asset::AlphaMode::Blend ? Blend
                                                                            : Opaque;
    rhi::ShaderProgram* program = variant == AlphaTest ? m_alphaTestProgram : m_program;
    device.bindPipeline(pipeline(variant, material.doubleSided));
    mesh.bind(device);
    bindMaterial(*program, device, material);
    mesh.draw(device, submesh);
}

void MeshRenderer::bindLighting(Device& device) const
{
    device.bindUniformBuffer(0, m_lightingBuffer);
    if (m_shadowMap != nullptr)
    {
        device.bindTexture(3, m_shadowMap->texture(), m_shadowMap->sampler());
    }
}

void MeshRenderer::draw(Device& device, const Mesh& mesh, const MaterialSet& materials, const Mat4& model,
                        const Camera& camera, i32 onlySubmesh, u32 lod)
{
    const Mat4 viewProjection = camera.viewProjection();
    device.bindUniformBuffer(0, m_lightingBuffer);
    if (m_shadowMap)
    {
        device.bindTexture(3, m_shadowMap->texture(), m_shadowMap->sampler());
    }
    m_selected.clear();
    if (m_lights)
    {
        m_lights->selectFor(mesh.bounds().transformed(model), m_selected);
    }
    std::array<i32, LightList::kMaxPerObject> indices{};
    for (usize i = 0; i < m_selected.size(); ++i)
    {
        indices[i] = static_cast<i32>(m_selected[i]);
    }
    for (rhi::ShaderProgram* program : {m_program, m_alphaTestProgram})
    {
        program->setUniform("uViewProjection", viewProjection);
        program->setUniform("uModel", model);
        program->setUniform("uCameraPosition", camera.transform.position);
        program->setUniform("uLightIndices", std::span<const i32>(indices));
        program->setUniform("uLightCount", static_cast<i32>(m_selected.size()));
    }
    const auto submeshes = mesh.submeshes();
    // Opaque and alpha-tested first, translucent last (blending needs what lies behind).
    for (const bool translucentPass : {false, true})
    {
        for (usize i = 0; i < submeshes.size(); ++i)
        {
            const Material& material = materials[submeshes[i].material];
            if ((material.alphaMode == asset::AlphaMode::Blend) == translucentPass && mesh.inLod(i, lod) &&
                (onlySubmesh < 0 || static_cast<usize>(onlySubmesh) == i))
            {
                drawSubmesh(device, mesh, i, material);
            }
        }
    }
}
void MeshRenderer::uploadBones(Device& device, std::span<const Mat4> bones)
{
    const usize count = std::min<usize>(bones.size(), asset::kMaxBones);
    (void)m_bonesBuffer.update(0, std::span(reinterpret_cast<const u8*>(bones.data()), count * sizeof(Mat4)));
    device.bindUniformBuffer(2, m_bonesBuffer);
}

void MeshRenderer::drawSkinned(Device& device, const SkinnedMesh& mesh, const MaterialSet& materials,
                               const Mat4& model, std::span<const Mat4> bones, const Camera& camera)
{
    uploadBones(device, bones);
    device.bindUniformBuffer(0, m_lightingBuffer);
    if (m_shadowMap)
    {
        device.bindTexture(3, m_shadowMap->texture(), m_shadowMap->sampler());
    }
    m_selected.clear();
    if (m_lights)
    {
        m_lights->selectFor(mesh.bounds().transformed(model), m_selected);
    }
    std::array<i32, LightList::kMaxPerObject> indices{};
    for (usize i = 0; i < m_selected.size(); ++i)
    {
        indices[i] = static_cast<i32>(m_selected[i]);
    }
    for (rhi::ShaderProgram* program : {m_skinnedProgram, m_skinnedAlphaTestProgram})
    {
        program->setUniform("uViewProjection", camera.viewProjection());
        program->setUniform("uModel", model);
        program->setUniform("uCameraPosition", camera.transform.position);
        program->setUniform("uLightIndices", std::span<const i32>(indices));
        program->setUniform("uLightCount", static_cast<i32>(m_selected.size()));
    }
    const auto submeshes = mesh.submeshes();
    for (const bool translucentPass : {false, true})
    {
        for (usize i = 0; i < submeshes.size(); ++i)
        {
            const Material& material = materials[submeshes[i].material];
            if ((material.alphaMode == asset::AlphaMode::Blend) != translucentPass)
            {
                continue;
            }
            const Variant variant = material.alphaMode == asset::AlphaMode::Mask    ? AlphaTest
                                    : material.alphaMode == asset::AlphaMode::Blend ? Blend
                                                                                    : Opaque;
            rhi::ShaderProgram* program = variant == AlphaTest ? m_skinnedAlphaTestProgram : m_skinnedProgram;
            device.bindPipeline(
                m_skinnedPipelines[static_cast<usize>(variant) * 2 + (material.doubleSided ? 1 : 0)]);
            mesh.bind(device);
            bindMaterial(*program, device, material);
            mesh.draw(device, i);
        }
    }
}

void MeshRenderer::drawShadowSkinned(Device& device, const SkinnedMesh& mesh, const MaterialSet& materials,
                                     const Mat4& model, std::span<const Mat4> bones, const Cascade& cascade)
{
    uploadBones(device, bones);
    for (rhi::ShaderProgram* program : {m_skinnedShadowProgram, m_skinnedShadowAlphaTestProgram})
    {
        program->setUniform("uViewProjection", cascade.viewProjection);
        program->setUniform("uModel", model);
    }
    const auto submeshes = mesh.submeshes();
    for (usize i = 0; i < submeshes.size(); ++i)
    {
        const Material& material = materials[submeshes[i].material];
        if (material.alphaMode == asset::AlphaMode::Blend)
        {
            continue;
        }
        const bool alphaTest = material.alphaMode == asset::AlphaMode::Mask;
        device.bindPipeline(m_skinnedShadowPipelines[alphaTest ? 1 : 0]);
        mesh.bind(device);
        if (alphaTest)
        {
            m_skinnedShadowAlphaTestProgram->setUniform("uBaseColor", material.baseColorFactor);
            m_skinnedShadowAlphaTestProgram->setUniform("uAlphaCutoff", material.alphaCutoff);
            device.bindTexture(0, *material.baseColor, m_sampler);
        }
        mesh.draw(device, i);
    }
}

void MeshRenderer::drawShadow(Device& device, const Mesh& mesh, const MaterialSet& materials,
                              const Mat4& model, const Cascade& cascade, u32 lod)
{
    for (rhi::ShaderProgram* program : {m_shadowProgram, m_shadowAlphaTestProgram})
    {
        program->setUniform("uViewProjection", cascade.viewProjection);
        program->setUniform("uModel", model);
    }
    const auto submeshes = mesh.submeshes();
    for (usize i = 0; i < submeshes.size(); ++i)
    {
        const Material& material = materials[submeshes[i].material];
        if (material.alphaMode == asset::AlphaMode::Blend || !mesh.inLod(i, lod))
        {
            continue;
        }
        const bool alphaTest = material.alphaMode == asset::AlphaMode::Mask;
        device.bindPipeline(m_shadowPipelines[alphaTest ? 1 : 0]);
        mesh.bind(device);
        if (alphaTest)
        {
            m_shadowAlphaTestProgram->setUniform("uBaseColor", material.baseColorFactor);
            m_shadowAlphaTestProgram->setUniform("uAlphaCutoff", material.alphaCutoff);
            device.bindTexture(0, *material.baseColor, m_sampler);
        }
        mesh.draw(device, i);
    }
}
} // namespace g7::render

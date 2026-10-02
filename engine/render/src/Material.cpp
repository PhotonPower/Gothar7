#include <g7/asset/ImageData.hpp>
#include <g7/core/Log.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/TextureUpload.hpp>

#include <map>
#include <utility>

namespace g7::render
{
Result<MaterialSet> MaterialSet::create(Device& device, const asset::MeshData& mesh,
                                        const fs::Path& modelDirectory)
{
    MaterialSet set;
    // Every texture lives in m_textures; indices first, pointers only once the vector is final.
    std::map<std::pair<i32, bool>, usize> uploaded; // (image, sRGB) -> texture index
    const auto fallback = [&](u8 r, u8 g, u8 b, bool srgb) -> Result<usize>
    {
        auto texture = createSolidTexture(device, r, g, b, 255, srgb);
        if (!texture)
        {
            return texture.error();
        }
        set.m_textures.push_back(std::move(texture).value());
        return set.m_textures.size() - 1;
    };
    auto white = fallback(255, 255, 255, true);
    auto flatNormal = fallback(128, 128, 255, false);
    if (!white || !flatNormal)
    {
        return Error{"cannot create fallback textures"};
    }

    const auto textureFor = [&](i32 image, bool srgb, usize fallbackIndex) -> usize
    {
        if (image < 0 || static_cast<usize>(image) >= mesh.images.size())
        {
            return fallbackIndex;
        }
        const auto key = std::make_pair(image, srgb);
        if (const auto it = uploaded.find(key); it != uploaded.end())
        {
            return it->second;
        }
        const asset::ImageSource& source = mesh.images[static_cast<usize>(image)];
        auto decoded = source.encoded.empty() ? asset::loadImage(modelDirectory / fs::fromUtf8(source.uri))
                                              : asset::decodeImage(source.encoded, "embedded image");
        auto texture = decoded ? createTexture(device, decoded.value(), {srgb, true})
                               : Result<rhi::Texture>(decoded.error());
        if (!texture)
        {
            G7_LOG_WARN("render", "texture skipped: {}", texture.error().message);
            uploaded.emplace(key, fallbackIndex);
            return fallbackIndex;
        }
        set.m_textures.push_back(std::move(texture).value());
        uploaded.emplace(key, set.m_textures.size() - 1);
        return set.m_textures.size() - 1;
    };

    struct Slots
    {
        usize baseColor, normal, emissive;
    };
    std::vector<Slots> slots;
    for (const asset::MaterialInfo& info : mesh.materials)
    {
        slots.push_back({textureFor(info.baseColorImage, true, white.value()),
                         textureFor(info.normalImage, false, flatNormal.value()),
                         textureFor(info.emissiveImage, true, white.value())});
    }

    for (usize i = 0; i < mesh.materials.size(); ++i)
    {
        const asset::MaterialInfo& info = mesh.materials[i];
        Material material;
        material.baseColor = &set.m_textures[slots[i].baseColor];
        material.normal = &set.m_textures[slots[i].normal];
        material.emissive = &set.m_textures[slots[i].emissive];
        material.baseColorFactor = info.baseColor;
        material.emissiveFactor = info.emissive;
        material.normalScale = info.normalScale;
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
    program->setUniform("uBaseColor", material.baseColorFactor);
    program->setUniform("uEmissive", material.emissiveFactor);
    program->setUniform("uNormalScale", material.normalScale);
    program->setUniform("uAlphaCutoff", material.alphaCutoff);
    device.bindTexture(0, *material.baseColor, m_sampler);
    device.bindTexture(1, *material.normal, m_sampler);
    device.bindTexture(2, *material.emissive, m_sampler);
    mesh.draw(device, submesh);
}

void MeshRenderer::draw(Device& device, const Mesh& mesh, const MaterialSet& materials, const Mat4& model,
                        const Camera& camera)
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
            if ((material.alphaMode == asset::AlphaMode::Blend) == translucentPass)
            {
                drawSubmesh(device, mesh, i, material);
            }
        }
    }
}
void MeshRenderer::drawShadow(Device& device, const Mesh& mesh, const MaterialSet& materials,
                              const Mat4& model, const Cascade& cascade)
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
        if (material.alphaMode == asset::AlphaMode::Blend)
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

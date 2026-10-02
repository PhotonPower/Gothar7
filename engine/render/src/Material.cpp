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

Result<MeshRenderer> MeshRenderer::create(Device& device, ShaderLibrary& shaders, f32 anisotropy)
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
    return renderer;
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
    for (rhi::ShaderProgram* program : {m_program, m_alphaTestProgram})
    {
        program->setUniform("uViewProjection", viewProjection);
        program->setUniform("uModel", model);
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
} // namespace g7::render

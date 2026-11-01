#include <g7/core/Log.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/ui/ItemPreview.hpp>

#include <algorithm>

namespace g7::ui
{
struct ItemPreviews::Gpu
{
    render::SceneTarget scene; // twice the tile size: the resolve averages 2 x 2 samples
    render::rhi::Framebuffer atlasTarget;
    render::rhi::ShaderProgram* program = nullptr; // owned by the ShaderLibrary
    render::rhi::Pipeline pipeline;
    render::rhi::Sampler sampler;
    render::LightList lights; // none; must outlive the draws (MeshRenderer::setLighting)
};

ItemPreviews::ItemPreviews() = default;
ItemPreviews::~ItemPreviews() = default;
ItemPreviews::ItemPreviews(ItemPreviews&&) noexcept = default;
ItemPreviews& ItemPreviews::operator=(ItemPreviews&&) noexcept = default;

Result<ItemPreviews> ItemPreviews::create(render::Device& device, render::ShaderLibrary& shaders,
                                          u32 tilePixels, u32 columns)
{
    if (tilePixels == 0 || columns == 0)
    {
        return Error{"item previews need a tile size and at least one column"};
    }
    ItemPreviews previews;
    previews.m_tile = tilePixels;
    previews.m_columns = columns;
    auto gpu = std::make_unique<Gpu>();
    auto scene = render::SceneTarget::create(device, tilePixels * 2, tilePixels * 2);
    if (!scene)
    {
        return scene.error();
    }
    gpu->scene = std::move(scene).value();
    auto atlas =
        device.createTexture({tilePixels * columns, tilePixels * columns, render::rhi::Format::RGBA8, 1});
    if (!atlas)
    {
        return atlas.error();
    }
    previews.m_atlas = std::move(atlas).value();
    auto target = device.createFramebuffer({{&previews.m_atlas}, nullptr});
    if (!target)
    {
        return target.error();
    }
    gpu->atlasTarget = std::move(target).value();
    auto program = shaders.load("item_preview", {"post.vert", "item_preview.frag", {}});
    if (!program)
    {
        return program.error();
    }
    gpu->program = program.value();
    render::rhi::PipelineDesc desc;
    desc.program = gpu->program;
    desc.cull = render::rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    auto pipeline = device.createPipeline(desc);
    if (!pipeline)
    {
        return pipeline.error();
    }
    gpu->pipeline = std::move(pipeline).value();
    render::rhi::SamplerDesc samplerDesc;
    samplerDesc.mipFilter = render::rhi::Filter::Nearest;
    samplerDesc.wrapU = samplerDesc.wrapV = render::rhi::Wrap::Clamp;
    auto sampler = device.createSampler(samplerDesc);
    if (!sampler)
    {
        return sampler.error();
    }
    gpu->sampler = std::move(sampler).value();
    previews.m_gpu = std::move(gpu);
    // Start empty (transparent).
    device.bindFramebuffer(&previews.m_gpu->atlasTarget);
    device.setViewport(0, 0, tilePixels * columns, tilePixels * columns);
    device.clear(Vec4(0.0f), std::nullopt);
    device.bindFramebuffer(nullptr);
    return previews;
}

Rect ItemPreviews::uv(u32 tile) const noexcept
{
    // Rendered tiles are upright in GL terms (row 0 at the bottom); the canvas puts uv.min at the quad's top
    // left, so v runs from the tile's top row down.
    const f32 step = 1.0f / static_cast<f32>(std::max(m_columns, 1u));
    const f32 u = static_cast<f32>(tile % m_columns) * step;
    const f32 v = static_cast<f32>(tile / m_columns) * step;
    return {Vec2(u, v + step), Vec2(u + step, v)};
}

void ItemPreviews::clear(render::Device& device, u32 tile)
{
    if (!m_gpu || tile >= capacity())
    {
        return;
    }
    device.bindFramebuffer(&m_gpu->atlasTarget);
    const auto x = static_cast<i32>((tile % m_columns) * m_tile);
    const auto y = static_cast<i32>((tile / m_columns) * m_tile);
    device.setScissor(render::PixelRect{x, y, m_tile, m_tile});
    device.clear(Vec4(0.0f), std::nullopt);
    device.setScissor(std::nullopt);
}

void ItemPreviews::render(render::Device& device, render::MeshRenderer& meshes, u32 tile,
                          const render::Mesh& mesh, const render::MaterialSet& materials,
                          const CellFrame& frame, f32 spinRadians, const PreviewLight& light)
{
    if (!m_gpu || tile >= capacity())
    {
        return;
    }
    Gpu& gpu = *m_gpu;
    // 1. The model into the HDR target: fixed studio light, no shadows, no fog.
    render::Environment environment;
    environment.sunDirection = glm::normalize(light.sunDirection);
    environment.sunColor = light.sunColor;
    environment.sunIntensity = light.sunIntensity;
    environment.ambientSky = light.ambientSky;
    environment.ambientGround = light.ambientGround;
    environment.fogDensity = 0.0f;
    gpu.lights.clear();
    meshes.setLighting(device, environment, gpu.lights);

    render::Camera camera;
    camera.transform.position = Vec3(0.0f, 0.0f, frame.distance);
    camera.fovY = toRadians(frame.fovDegrees);
    camera.aspect = 1.0f;
    camera.nearPlane = std::max(0.001f, frame.distance * 0.02f);
    camera.farPlane = frame.distance * 4.0f;
    camera.windowCenter = frame.window;
    camera.windowHalfSize = frame.halfSize;

    device.bindFramebuffer(&gpu.scene.framebuffer());
    device.setViewport(0, 0, gpu.scene.width(), gpu.scene.height());
    device.clear(Vec4(0.0f), 0.0f); // reverse-Z: far is 0
    meshes.draw(device, mesh, materials, frame.modelMatrix(spinRadians), camera);

    // 2. Tonemapped into the tile.
    device.bindFramebuffer(&gpu.atlasTarget);
    device.setViewport(static_cast<i32>((tile % m_columns) * m_tile),
                       static_cast<i32>((tile / m_columns) * m_tile), m_tile, m_tile);
    gpu.program->setUniform("uExposure", light.exposure);
    device.bindPipeline(gpu.pipeline);
    device.bindTexture(0, gpu.scene.color(), gpu.sampler);
    device.draw(3);
}
} // namespace g7::ui

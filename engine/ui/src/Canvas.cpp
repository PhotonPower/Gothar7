#include <g7/core/Log.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/TextureUpload.hpp>
#include <g7/ui/Canvas.hpp>
#include <g7/ui/DebugUi.hpp>
#include <g7/ui/Font.hpp>

#include <algorithm>
#include <array>
#include <cstring>

namespace g7::ui
{
static_assert(sizeof(Canvas::Vertex) == 32, "ui.vert expects 32-byte vertices");

Rect Rect::intersect(const Rect& other) const noexcept
{
    return {glm::max(min, other.min), glm::min(max, other.max)};
}

struct Canvas::Gpu
{
    render::rhi::ShaderProgram* program = nullptr; // owned by the ShaderLibrary
    render::rhi::Pipeline pipeline;
    render::rhi::Sampler sampler;
    render::rhi::Texture white;
    render::rhi::Buffer vertexBuffer;
    render::rhi::Buffer indexBuffer;

    bool reserve(render::Device& device, render::rhi::Buffer& buffer, usize bytes)
    {
        if (buffer.size() >= bytes)
        {
            return true;
        }
        auto grown = device.createBuffer(
            {std::max<usize>(bytes + bytes / 2, 64 * 1024), render::rhi::BufferUsage::Dynamic, {}});
        if (!grown)
        {
            G7_LOG_ERROR("ui", "canvas buffer: {}", grown.error().message);
            return false;
        }
        buffer = std::move(grown).value();
        return true;
    }
};

Canvas::Canvas() = default;
Canvas::~Canvas() = default;
Canvas::Canvas(Canvas&&) noexcept = default;
Canvas& Canvas::operator=(Canvas&&) noexcept = default;

Result<Canvas> Canvas::create(render::Device* device, render::ShaderLibrary* shaders)
{
    Canvas canvas;
    if (device == nullptr || shaders == nullptr)
    {
        return canvas;
    }
    auto gpu = std::make_unique<Gpu>();
    auto program = shaders->load("ui", {"ui.vert", "ui.frag", {}});
    if (!program)
    {
        return program.error();
    }
    gpu->program = program.value();
    render::rhi::PipelineDesc desc;
    desc.program = gpu->program;
    desc.attributes = {
        {0, render::rhi::VertexFormat::Float2, 0},    {1, render::rhi::VertexFormat::Float2, 8},
        {2, render::rhi::VertexFormat::UNorm8x4, 16}, {3, render::rhi::VertexFormat::Float1, 20},
        {4, render::rhi::VertexFormat::Float1, 24},   {5, render::rhi::VertexFormat::UNorm8x4, 28}};
    desc.vertexStride = sizeof(Vertex);
    desc.cull = render::rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.blend = render::rhi::BlendMode::Alpha;
    auto pipeline = device->createPipeline(desc);
    if (!pipeline)
    {
        return pipeline.error();
    }
    gpu->pipeline = std::move(pipeline).value();
    render::rhi::SamplerDesc samplerDesc;
    samplerDesc.wrapU = samplerDesc.wrapV = render::rhi::Wrap::Clamp;
    auto sampler = device->createSampler(samplerDesc);
    if (!sampler)
    {
        return sampler.error();
    }
    gpu->sampler = std::move(sampler).value();
    auto white = render::createSolidTexture(*device, 255, 255, 255, 255);
    if (!white)
    {
        return white.error();
    }
    gpu->white = std::move(white).value();
    canvas.m_gpu = std::move(gpu);
    return canvas;
}

void Canvas::begin(Vec2 pixels, f32 uiScale)
{
    m_pixels = pixels;
    m_uiScale = uiScale;
    m_vertices.clear();
    m_indices.clear();
    m_commands.clear();
    m_clips.clear();
}

std::optional<Rect> Canvas::currentClip() const
{
    if (m_clips.empty())
    {
        return std::nullopt;
    }
    return m_clips.back();
}

void Canvas::quad(const Rect& rect, const Rect& uv, const render::rhi::Texture* texture, u32 color,
                  f32 pixelRange, f32 outline, u32 outlineColor)
{
    if (rect.empty())
    {
        return;
    }
    const std::optional<Rect> clip = currentClip();
    if (clip && rect.intersect(*clip).empty())
    {
        return; // entirely clipped away
    }
    if (m_commands.empty() || m_commands.back().texture != texture || m_commands.back().clip != clip)
    {
        m_commands.push_back({texture, clip, static_cast<u32>(m_indices.size()), 0});
    }
    const auto base = static_cast<u32>(m_vertices.size());
    const std::array<Vec2, 4> corners = {rect.min, Vec2(rect.max.x, rect.min.y), rect.max,
                                         Vec2(rect.min.x, rect.max.y)};
    const std::array<Vec2, 4> uvs = {uv.min, Vec2(uv.max.x, uv.min.y), uv.max, Vec2(uv.min.x, uv.max.y)};
    for (usize i = 0; i < 4; ++i)
    {
        m_vertices.push_back({corners[i], uvs[i], color, pixelRange, outline, outlineColor});
    }
    for (const u32 i : {0u, 1u, 2u, 0u, 2u, 3u})
    {
        m_indices.push_back(base + i);
    }
    m_commands.back().indexCount += 6;
}

void Canvas::rect(const Rect& rect, Color color)
{
    quad(rect, {Vec2(0.0f), Vec2(1.0f)}, nullptr, color.packed());
}

void Canvas::image(const Rect& rect, const render::rhi::Texture* texture, const Rect& uv, Color tint,
                   bool displayValues)
{
    quad(rect, uv, texture, tint.packed(), displayValues ? -1.0f : 0.0f);
}

void Canvas::frame(const Rect& rect, const NineSlice& slice, Color tint)
{
    if (slice.texture == nullptr || slice.imageSize.x <= 0.0f || slice.imageSize.y <= 0.0f)
    {
        this->rect(rect, tint);
        return;
    }
    f32 left = slice.left * slice.borderScale;
    f32 right = slice.right * slice.borderScale;
    f32 top = slice.top * slice.borderScale;
    f32 bottom = slice.bottom * slice.borderScale;
    if (left + right > rect.width() && left + right > 0.0f)
    {
        const f32 shrink = rect.width() / (left + right);
        left *= shrink;
        right *= shrink;
    }
    if (top + bottom > rect.height() && top + bottom > 0.0f)
    {
        const f32 shrink = rect.height() / (top + bottom);
        top *= shrink;
        bottom *= shrink;
    }
    const std::array<f32, 4> xs = {rect.min.x, rect.min.x + left, rect.max.x - right, rect.max.x};
    const std::array<f32, 4> ys = {rect.min.y, rect.min.y + top, rect.max.y - bottom, rect.max.y};
    const std::array<f32, 4> us = {0.0f, slice.left / slice.imageSize.x,
                                   1.0f - slice.right / slice.imageSize.x, 1.0f};
    const std::array<f32, 4> vs = {0.0f, slice.top / slice.imageSize.y,
                                   1.0f - slice.bottom / slice.imageSize.y, 1.0f};
    const u32 color = tint.packed();
    for (usize row = 0; row < 3; ++row)
    {
        for (usize column = 0; column < 3; ++column)
        {
            quad({Vec2(xs[column], ys[row]), Vec2(xs[column + 1], ys[row + 1])},
                 {Vec2(us[column], vs[row]), Vec2(us[column + 1], vs[row + 1])}, slice.texture, color);
        }
    }
}

void Canvas::text(Vec2 position, const TextLayout& layout, const TextStyle& style)
{
    if (style.font == nullptr)
    {
        return;
    }
    const render::rhi::Texture* texture = style.font->texture();
    if (texture == nullptr && m_gpu)
    {
        return; // not uploaded: nothing to sample
    }
    const f32 range = style.font->screenPixelRange(layout.pixelSize);
    const u32 color = style.color.packed();
    const u32 outlineColor = style.outlineColor.packed();
    if (style.shadow > 0.0f)
    {
        // A soft drop shadow: the glyphs once more, offset, in the shadow colour (wider edge = softer).
        const Vec2 offset(style.shadow);
        const u32 shadowColor = style.shadowColor.packed();
        for (const PlacedGlyph& g : layout.glyphs)
        {
            quad({position + offset + g.min, position + offset + g.max}, {g.uvMin, g.uvMax}, texture,
                 shadowColor, std::max(1.0f, range * 0.5f), 0.0f, 0);
        }
    }
    // A glyph quad of an un-uploaded font (tests) is marked with the white texture's nullptr but keeps its
    // range.
    for (const PlacedGlyph& g : layout.glyphs)
    {
        quad({position + g.min, position + g.max}, {g.uvMin, g.uvMax}, texture, color, range, style.outline,
             outlineColor);
    }
}

void Canvas::pushClip(const Rect& clip)
{
    const std::optional<Rect> current = currentClip();
    m_clips.push_back(current ? current->intersect(clip) : clip);
}

void Canvas::popClip()
{
    if (!m_clips.empty())
    {
        m_clips.pop_back();
    }
}

void Canvas::end(render::Device* device)
{
    if (device != nullptr && m_gpu && !m_indices.empty() && m_pixels.x >= 1.0f && m_pixels.y >= 1.0f)
    {
        Gpu& gpu = *m_gpu;
        const std::span<const u8> vertexBytes(reinterpret_cast<const u8*>(m_vertices.data()),
                                              m_vertices.size() * sizeof(Vertex));
        const std::span<const u8> indexBytes(reinterpret_cast<const u8*>(m_indices.data()),
                                             m_indices.size() * sizeof(u32));
        if (gpu.reserve(*device, gpu.vertexBuffer, vertexBytes.size()) &&
            gpu.reserve(*device, gpu.indexBuffer, indexBytes.size()) &&
            gpu.vertexBuffer.update(0, vertexBytes) && gpu.indexBuffer.update(0, indexBytes))
        {
            const auto width = static_cast<u32>(m_pixels.x);
            const auto height = static_cast<u32>(m_pixels.y);
            Mat4 projection(1.0f); // pixels (+Y down) to clip space
            projection[0][0] = 2.0f / m_pixels.x;
            projection[1][1] = -2.0f / m_pixels.y;
            projection[2][2] = -1.0f;
            projection[3] = Vec4(-1.0f, 1.0f, 0.0f, 1.0f);
            device->setViewport(0, 0, width, height);
            gpu.program->setUniform("uProjection", projection);
            device->bindPipeline(gpu.pipeline);
            device->bindVertexBuffer(gpu.vertexBuffer);
            device->bindIndexBuffer(gpu.indexBuffer, render::rhi::IndexType::U32);
            for (const Command& command : m_commands)
            {
                if (command.clip)
                {
                    const auto scissor = scissorFromClip(Vec4(command.clip->min.x, command.clip->min.y,
                                                              command.clip->max.x, command.clip->max.y),
                                                         width, height);
                    if (!scissor)
                    {
                        continue;
                    }
                    device->setScissor(scissor);
                }
                else
                {
                    device->setScissor(std::nullopt);
                }
                device->bindTexture(0, command.texture != nullptr ? *command.texture : gpu.white,
                                    gpu.sampler);
                device->drawIndexed(command.indexCount, command.firstIndex);
            }
            device->setScissor(std::nullopt);
        }
    }
    m_vertices.clear();
    m_indices.clear();
    m_commands.clear();
    m_clips.clear();
}
} // namespace g7::ui

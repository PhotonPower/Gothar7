#include "DebugFont.hpp"

#include <g7/core/Log.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace g7::render
{
namespace
{
constexpr u32 kAtlasColumns = 16;
constexpr u32 kAtlasRows = (detail::kDebugGlyphCount + kAtlasColumns - 1) / kAtlasColumns;
constexpr u32 kAtlasWidth = kAtlasColumns * 8;
constexpr u32 kAtlasHeight = kAtlasRows * 8;
constexpr u32 kShadowColor = 0xC0000000u; // black, 75 % opaque
constexpr i32 kNewLine = -1;

struct LineVertex
{
    Vec3 position;
    u32 color;
};
static_assert(sizeof(LineVertex) == 16);

struct TextVertex
{
    Vec2 pixel;
    Vec2 uv;
    u32 color;
    f32 depth;
};
static_assert(sizeof(TextVertex) == 24);

/// Two unit vectors perpendicular to `normal` and to each other.
void basis(const Vec3& normal, Vec3& u, Vec3& v)
{
    const Vec3 n = glm::normalize(normal);
    const Vec3 helper = std::abs(n.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    u = glm::normalize(glm::cross(helper, n));
    v = glm::cross(n, u);
}

std::vector<u8> buildFontAtlas()
{
    std::vector<u8> pixels(static_cast<usize>(kAtlasWidth) * kAtlasHeight, 0);
    for (u32 glyph = 0; glyph < detail::kDebugGlyphCount; ++glyph)
    {
        const u32 x0 = (glyph % kAtlasColumns) * 8;
        const u32 y0 = (glyph / kAtlasColumns) * 8;
        for (u32 row = 0; row < 8; ++row)
        {
            const u8 bits = detail::kDebugFont[glyph][row];
            for (u32 column = 0; column < 8; ++column)
            {
                if ((bits >> column) & 1u)
                {
                    pixels[static_cast<usize>(y0 + row) * kAtlasWidth + x0 + column] = 255;
                }
            }
        }
    }
    return pixels;
}

template <typename T>
void appendBytes(std::vector<u8>& out, const T& value)
{
    const auto* bytes = reinterpret_cast<const u8*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}
} // namespace

// --- DebugDraw ---

u32 DebugDraw::packColor(const Vec4& color) noexcept
{
    const auto channel = [](f32 value)
    { return static_cast<u32>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
    return channel(color.r) | (channel(color.g) << 8) | (channel(color.b) << 16) | (channel(color.a) << 24);
}

std::vector<i32> DebugDraw::glyphs(std::string_view text)
{
    std::vector<i32> out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        const auto c = static_cast<u8>(ch);
        if (c == '\n')
        {
            out.push_back(kNewLine);
        }
        else if (c >= 0x20 && c <= 0x7E)
        {
            out.push_back(c - 0x20);
        }
        else if (c >= 0xC0)
        {
            out.push_back('?' - 0x20); // lead byte of a multi-byte code point
        }
        // continuation bytes (0x80..0xBF) and other control characters: nothing
    }
    return out;
}

void DebugDraw::line(const Vec3& a, const Vec3& b, const DebugStyle& style)
{
    m_lines.push_back({a, b, packColor(style.color), style.duration, style.depthTest});
}

void DebugDraw::arrow(const Vec3& from, const Vec3& to, const DebugStyle& style)
{
    line(from, to, style);
    const Vec3 direction = to - from;
    const f32 length = glm::length(direction);
    if (length <= 0.0f)
    {
        return;
    }
    Vec3 u;
    Vec3 v;
    basis(direction, u, v);
    const f32 head = length * 0.2f;
    const Vec3 back = to - direction / length * head;
    for (const Vec3& side : {u, -u, v, -v})
    {
        line(to, back + side * (head * 0.4f), style);
    }
}

void DebugDraw::cross(const Vec3& point, f32 size, const DebugStyle& style)
{
    const f32 h = size * 0.5f;
    line(point - Vec3(h, 0, 0), point + Vec3(h, 0, 0), style);
    line(point - Vec3(0, h, 0), point + Vec3(0, h, 0), style);
    line(point - Vec3(0, 0, h), point + Vec3(0, 0, h), style);
}

void DebugDraw::box(const AABB& bounds, const DebugStyle& style)
{
    const Vec3 centre = (bounds.min + bounds.max) * 0.5f;
    box(glm::translate(Mat4(1.0f), centre), (bounds.max - bounds.min) * 0.5f, style);
}

void DebugDraw::box(const Mat4& transform, const Vec3& halfExtents, const DebugStyle& style)
{
    std::array<Vec3, 8> corners;
    for (u32 i = 0; i < 8; ++i)
    {
        const Vec3 local((i & 1) ? halfExtents.x : -halfExtents.x, (i & 2) ? halfExtents.y : -halfExtents.y,
                         (i & 4) ? halfExtents.z : -halfExtents.z);
        corners[i] = Vec3(transform * Vec4(local, 1.0f));
    }
    // Edges connect corners differing in exactly one bit.
    for (u32 i = 0; i < 8; ++i)
    {
        for (const u32 bit : {1u, 2u, 4u})
        {
            if ((i & bit) == 0)
            {
                line(corners[i], corners[i | bit], style);
            }
        }
    }
}

void DebugDraw::circle(const Vec3& centre, const Vec3& normal, f32 radius, const DebugStyle& style)
{
    Vec3 u;
    Vec3 v;
    basis(normal, u, v);
    Vec3 previous = centre + u * radius;
    for (u32 i = 1; i <= kCircleSegments; ++i)
    {
        const f32 angle = 2.0f * kPi * static_cast<f32>(i) / static_cast<f32>(kCircleSegments);
        const Vec3 next = centre + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        line(previous, next, style);
        previous = next;
    }
}

void DebugDraw::sphere(const Vec3& centre, f32 radius, const DebugStyle& style)
{
    circle(centre, Vec3(1, 0, 0), radius, style);
    circle(centre, Vec3(0, 1, 0), radius, style);
    circle(centre, Vec3(0, 0, 1), radius, style);
}

void DebugDraw::axes(const Mat4& transform, f32 size, f32 duration)
{
    const Vec3 origin(transform[3]);
    line(origin, Vec3(transform * Vec4(size, 0, 0, 1)), {Vec4(1, 0.2f, 0.2f, 1), duration});
    line(origin, Vec3(transform * Vec4(0, size, 0, 1)), {Vec4(0.2f, 1, 0.2f, 1), duration});
    line(origin, Vec3(transform * Vec4(0, 0, size, 1)), {Vec4(0.3f, 0.5f, 1, 1), duration});
}

void DebugDraw::frustum(const Mat4& viewProjection, const DebugStyle& style)
{
    const Mat4 inverse = glm::inverse(viewProjection);
    std::array<Vec3, 8> corners;
    for (u32 i = 0; i < 8; ++i)
    {
        // Clip depth is 0..1 (glClipControl zero-to-one), whichever end is near.
        const Vec4 clip((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
        const Vec4 world = inverse * clip;
        corners[i] = Vec3(world) / world.w;
    }
    for (u32 i = 0; i < 8; ++i)
    {
        for (const u32 bit : {1u, 2u, 4u})
        {
            if ((i & bit) == 0)
            {
                line(corners[i], corners[i | bit], style);
            }
        }
    }
}

void DebugDraw::grid(const Vec3& centre, f32 size, f32 spacing, const DebugStyle& style)
{
    if (spacing <= 0.0f || size <= 0.0f)
    {
        return;
    }
    const f32 h = size * 0.5f;
    const auto steps = static_cast<i32>(std::floor(h / spacing));
    for (i32 i = -steps; i <= steps; ++i)
    {
        const f32 offset = static_cast<f32>(i) * spacing;
        line(centre + Vec3(offset, 0, -h), centre + Vec3(offset, 0, h), style);
        line(centre + Vec3(-h, 0, offset), centre + Vec3(h, 0, offset), style);
    }
}

void DebugDraw::text(const Vec3& position, std::string_view text, const DebugStyle& style, f32 scale)
{
    m_texts.push_back(
        {std::string(text), position, packColor(style.color), scale, style.duration, style.depthTest, false});
}

void DebugDraw::screenText(const Vec2& pixels, std::string_view text, const Vec4& color, f32 scale,
                           f32 duration)
{
    m_texts.push_back(
        {std::string(text), Vec3(pixels, 0.0f), packColor(color), scale, duration, false, true});
}

void DebugDraw::advance(f32 deltaSeconds)
{
    for (Line& l : m_lines)
    {
        l.remaining -= deltaSeconds;
    }
    for (Text& t : m_texts)
    {
        t.remaining -= deltaSeconds;
    }
    // Single-frame items (remaining 0) are gone after any advance, even with deltaSeconds 0.
    std::erase_if(m_lines, [](const Line& l) { return l.remaining <= 0.0f; });
    std::erase_if(m_texts, [](const Text& t) { return t.remaining <= 0.0f; });
}

void DebugDraw::clear()
{
    m_lines.clear();
    m_texts.clear();
}

void layoutDebugText(const DebugDraw::Text& text, const Mat4& viewProjection, u32 width, u32 height,
                     std::vector<DebugGlyphQuad>& out)
{
    const std::vector<i32> glyphs = DebugDraw::glyphs(text.text);
    if (glyphs.empty())
    {
        return;
    }
    const f32 size = std::max(std::round(DebugDraw::kGlyphSize * text.scale), 1.0f);
    u32 lines = 1;
    u32 longest = 0;
    u32 current = 0;
    for (const i32 g : glyphs)
    {
        current = g == kNewLine ? 0 : current + 1;
        lines += g == kNewLine ? 1 : 0;
        longest = std::max(longest, current);
    }

    Vec2 origin;
    f32 depth = -1.0f;
    if (text.screen)
    {
        origin = Vec2(text.position);
    }
    else
    {
        const Vec4 clip = viewProjection * Vec4(text.position, 1.0f);
        if (clip.w <= 1e-6f)
        {
            return; // behind the camera
        }
        const Vec3 ndc = Vec3(clip) / clip.w;
        const Vec2 anchor((ndc.x * 0.5f + 0.5f) * static_cast<f32>(width),
                          (0.5f - ndc.y * 0.5f) * static_cast<f32>(height));
        origin = anchor - Vec2(static_cast<f32>(longest), static_cast<f32>(lines)) * size * 0.5f;
        depth = text.depthTest ? ndc.z : -1.0f;
    }
    origin = glm::floor(origin); // whole pixels keep the glyphs crisp

    const auto emit = [&](const Vec2& offset, u32 color)
    {
        Vec2 pen = origin + offset;
        for (const i32 g : glyphs)
        {
            if (g == kNewLine)
            {
                pen = Vec2(origin.x + offset.x, pen.y + size);
                continue;
            }
            if (g != 0) // spaces need no quad
            {
                out.push_back({pen, pen + Vec2(size), g, color, depth});
            }
            pen.x += size;
        }
    };
    const u32 alpha = text.color >> 24;
    const u32 shadow = (((kShadowColor >> 24) * alpha / 255u) << 24);
    const f32 shadowOffset = std::max(std::round(text.scale), 1.0f);
    emit(Vec2(shadowOffset), shadow);
    emit(Vec2(0.0f), text.color);
}

// --- DebugDrawRenderer ---

Result<DebugDrawRenderer> DebugDrawRenderer::create(Device& device, ShaderLibrary& shaders)
{
    DebugDrawRenderer r;
    auto lineProgram = shaders.load("debug_line", {"debug_line.vert", "debug_line.frag", {}});
    if (!lineProgram)
    {
        return lineProgram.error();
    }
    auto textProgram = shaders.load("debug_text", {"debug_text.vert", "debug_text.frag", {}});
    if (!textProgram)
    {
        return textProgram.error();
    }
    r.m_lineProgram = lineProgram.value();
    r.m_textProgram = textProgram.value();

    rhi::PipelineDesc lines;
    lines.program = r.m_lineProgram;
    lines.attributes = {{0, rhi::VertexFormat::Float3, 0}, {1, rhi::VertexFormat::UNorm8x4, 12}};
    lines.vertexStride = sizeof(LineVertex);
    lines.topology = rhi::Topology::Lines;
    lines.cull = rhi::CullMode::None;
    lines.depthTest = false; // tested in the shader against the scene depth
    lines.depthWrite = false;
    lines.blend = rhi::BlendMode::Alpha;
    auto linePipeline = device.createPipeline(lines);
    if (!linePipeline)
    {
        return linePipeline.error();
    }
    r.m_linePipeline = std::move(linePipeline).value();

    rhi::PipelineDesc text = lines;
    text.program = r.m_textProgram;
    text.attributes = {{0, rhi::VertexFormat::Float2, 0},
                       {1, rhi::VertexFormat::Float2, 8},
                       {2, rhi::VertexFormat::UNorm8x4, 16},
                       {3, rhi::VertexFormat::Float1, 20}};
    text.vertexStride = sizeof(TextVertex);
    text.topology = rhi::Topology::Triangles;
    auto textPipeline = device.createPipeline(text);
    if (!textPipeline)
    {
        return textPipeline.error();
    }
    r.m_textPipeline = std::move(textPipeline).value();

    auto font = device.createTexture({kAtlasWidth, kAtlasHeight, rhi::Format::R8, 1});
    if (!font)
    {
        return font.error();
    }
    r.m_font = std::move(font).value();
    const std::vector<u8> atlas = buildFontAtlas();
    if (auto uploaded = r.m_font.upload(0, atlas); !uploaded)
    {
        return uploaded.error();
    }
    rhi::SamplerDesc nearest;
    nearest.minFilter = nearest.magFilter = nearest.mipFilter = rhi::Filter::Nearest;
    nearest.wrapU = nearest.wrapV = rhi::Wrap::Clamp;
    auto sampler = device.createSampler(nearest);
    if (!sampler)
    {
        return sampler.error();
    }
    r.m_nearest = std::move(sampler).value();
    return r;
}

bool DebugDrawRenderer::reserve(Device& device, rhi::Buffer& buffer, usize bytes)
{
    if (buffer.size() >= bytes)
    {
        return true;
    }
    auto grown = device.createBuffer(
        {std::bit_ceil(std::max<usize>(bytes, 64 * 1024)), rhi::BufferUsage::Dynamic, {}});
    if (!grown)
    {
        G7_LOG_ERROR("render", "debug draw: {}", grown.error().message);
        return false;
    }
    buffer = std::move(grown).value();
    return true;
}

void DebugDrawRenderer::render(Device& device, const DebugDraw& debug, const Camera& camera,
                               const rhi::Texture* sceneDepth, u32 width, u32 height)
{
    if (!debug.enabled || width == 0 || height == 0 || (debug.lines().empty() && debug.texts().empty()))
    {
        return;
    }
    device.setViewport(0, 0, width, height);
    const i32 hasDepth = sceneDepth != nullptr ? 1 : 0;
    const Vec2 viewport(static_cast<f32>(width), static_cast<f32>(height));

    if (!debug.lines().empty())
    {
        m_staging.clear();
        m_staging.reserve(debug.lines().size() * 2 * sizeof(LineVertex));
        u32 tested = 0;
        // Depth-tested lines first, then the always-visible ones (same buffer, two draws).
        for (const bool depthTest : {true, false})
        {
            for (const DebugDraw::Line& l : debug.lines())
            {
                if (l.depthTest == depthTest)
                {
                    appendBytes(m_staging, LineVertex{l.a, l.color});
                    appendBytes(m_staging, LineVertex{l.b, l.color});
                    tested += depthTest ? 2 : 0;
                }
            }
        }
        if (reserve(device, m_lineBuffer, m_staging.size()) && m_lineBuffer.update(0, m_staging))
        {
            const auto total = static_cast<u32>(m_staging.size() / sizeof(LineVertex));
            m_lineProgram->setUniform("uViewProjection", camera.viewProjection());
            m_lineProgram->setUniform("uHasSceneDepth", hasDepth);
            device.bindPipeline(m_linePipeline);
            device.bindVertexBuffer(m_lineBuffer);
            if (sceneDepth != nullptr)
            {
                device.bindTexture(0, *sceneDepth, m_nearest);
            }
            if (tested > 0)
            {
                m_lineProgram->setUniform("uDepthTest", 1);
                device.draw(tested);
            }
            if (total > tested)
            {
                m_lineProgram->setUniform("uDepthTest", 0);
                device.draw(total - tested, tested);
            }
        }
    }

    if (!debug.texts().empty())
    {
        m_quads.clear();
        const Mat4 viewProjection = camera.viewProjection();
        for (const DebugDraw::Text& t : debug.texts())
        {
            layoutDebugText(t, viewProjection, width, height, m_quads);
        }
        m_staging.clear();
        m_staging.reserve(m_quads.size() * 6 * sizeof(TextVertex));
        const Vec2 texel(1.0f / kAtlasWidth, 1.0f / kAtlasHeight);
        for (const DebugGlyphQuad& q : m_quads)
        {
            const Vec2 uv0 = Vec2(static_cast<f32>((q.glyph % kAtlasColumns) * 8),
                                  static_cast<f32>((q.glyph / kAtlasColumns) * 8)) *
                             texel;
            const Vec2 uv1 = uv0 + Vec2(8.0f) * texel;
            const TextVertex tl{q.min, uv0, q.color, q.depth};
            const TextVertex tr{{q.max.x, q.min.y}, {uv1.x, uv0.y}, q.color, q.depth};
            const TextVertex bl{{q.min.x, q.max.y}, {uv0.x, uv1.y}, q.color, q.depth};
            const TextVertex br{q.max, uv1, q.color, q.depth};
            for (const TextVertex& v : {tl, bl, br, tl, br, tr})
            {
                appendBytes(m_staging, v);
            }
        }
        if (!m_staging.empty() && reserve(device, m_textBuffer, m_staging.size()) &&
            m_textBuffer.update(0, m_staging))
        {
            m_textProgram->setUniform("uViewport", viewport);
            m_textProgram->setUniform("uHasSceneDepth", hasDepth);
            device.bindPipeline(m_textPipeline);
            device.bindVertexBuffer(m_textBuffer);
            if (sceneDepth != nullptr)
            {
                device.bindTexture(0, *sceneDepth, m_nearest);
            }
            device.bindTexture(1, m_font, m_nearest);
            device.draw(static_cast<u32>(m_staging.size() / sizeof(TextVertex)));
        }
    }
}
} // namespace g7::render

#pragma once

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace g7::render
{
class Device;
class ShaderLibrary;
struct Camera;

/// How a debug item is drawn. Colours are display (sRGB) values: debug drawing happens after
/// tonemapping, directly into the window.
struct DebugStyle
{
    Vec4 color{1.0f};
    f32 duration = 0.0f;   ///< Seconds to keep the item; 0 = this frame only.
    bool depthTest = true; ///< Hidden parts are drawn faint and dashed instead of full.
};

/// Immediate-mode debug geometry (render.md): lines, shapes and text in the world or on screen.
/// Collects items on the CPU; DebugDrawRenderer draws them. Call advance() once per frame after
/// drawing to age and drop items.
class DebugDraw
{
public:
    static constexpr u32 kCircleSegments = 32;
    static constexpr f32 kGlyphSize = 8.0f; ///< Pixels per glyph at scale 1.

    struct Line
    {
        Vec3 a;
        Vec3 b;
        u32 color; ///< RGBA8, R in the lowest byte
        f32 remaining;
        bool depthTest;
    };

    struct Text
    {
        std::string text;
        Vec3 position; ///< world position, or pixels (top-left origin) for screen text
        u32 color;
        f32 scale;
        f32 remaining;
        bool depthTest;
        bool screen;
    };

    bool enabled = true; ///< Off: items are still collected and aged, but not drawn.

    void line(const Vec3& a, const Vec3& b, const DebugStyle& style = {});
    /// Line with a four-line arrow head at `to`.
    void arrow(const Vec3& from, const Vec3& to, const DebugStyle& style = {});
    /// Three axis-aligned lines through `point`, `size` long each.
    void cross(const Vec3& point, f32 size, const DebugStyle& style = {});
    void box(const AABB& box, const DebugStyle& style = {});
    /// Oriented box: `halfExtents` around the origin, placed by `transform`.
    void box(const Mat4& transform, const Vec3& halfExtents, const DebugStyle& style = {});
    void circle(const Vec3& centre, const Vec3& normal, f32 radius, const DebugStyle& style = {});
    /// Three great circles (XY, XZ, YZ).
    void sphere(const Vec3& centre, f32 radius, const DebugStyle& style = {});
    /// Red X, green Y, blue Z axes of `transform`, `size` long.
    void axes(const Mat4& transform, f32 size, f32 duration = 0.0f);
    /// The 12 edges of the volume `viewProjection` maps to clip space (a camera, a shadow cascade).
    void frustum(const Mat4& viewProjection, const DebugStyle& style = {});
    /// Square grid in the XZ plane around `centre`.
    void grid(const Vec3& centre, f32 size, f32 spacing, const DebugStyle& style = {});

    /// Text centred on a world point, screen-aligned at a constant pixel size. ASCII; other
    /// characters show as '?'; '\n' starts a new line.
    void text(const Vec3& position, std::string_view text, const DebugStyle& style = {}, f32 scale = 1.0f);
    /// Text at a pixel position (top-left origin, top-left of the first glyph). Never depth-tested.
    void screenText(const Vec2& pixels, std::string_view text, const Vec4& color = Vec4(1.0f),
                    f32 scale = 1.0f, f32 duration = 0.0f);

    /// Ages items by `deltaSeconds` and drops expired ones (and all single-frame items).
    void advance(f32 deltaSeconds);
    void clear();

    [[nodiscard]] const std::vector<Line>& lines() const noexcept { return m_lines; }
    [[nodiscard]] const std::vector<Text>& texts() const noexcept { return m_texts; }
    [[nodiscard]] usize lineCount() const noexcept { return m_lines.size(); }

    [[nodiscard]] static u32 packColor(const Vec4& color) noexcept;
    /// Glyph index (0..94 for ' '..'~') per character of UTF-8 `text`: one '?' per non-ASCII
    /// code point, control characters except '\n' (returned as -1) dropped.
    [[nodiscard]] static std::vector<i32> glyphs(std::string_view text);

private:
    std::vector<Line> m_lines;
    std::vector<Text> m_texts;
};

/// One screen-space glyph quad (pixels, top-left origin).
struct DebugGlyphQuad
{
    Vec2 min;
    Vec2 max;
    i32 glyph;
    u32 color;
    f32 depth; ///< window depth of the anchor (reverse-Z), < 0 = not depth-tested
};

/// Lays out `text` for a viewport: projects world text (nothing behind the camera), centres it,
/// adds a dark one-pixel shadow per glyph for legibility.
void layoutDebugText(const DebugDraw::Text& text, const Mat4& viewProjection, u32 width, u32 height,
                     std::vector<DebugGlyphQuad>& out);

/// Draws a DebugDraw into the bound target (normally the window, after the post pass). Depth
/// testing reads the scene's depth texture, so it must have the viewport's size.
class DebugDrawRenderer
{
public:
    DebugDrawRenderer() = default;
    [[nodiscard]] static Result<DebugDrawRenderer> create(Device& device, ShaderLibrary& shaders);

    /// `sceneDepth` may be null (nothing counts as hidden).
    void render(Device& device, const DebugDraw& debug, const Camera& camera, const rhi::Texture* sceneDepth,
                u32 width, u32 height);

private:
    [[nodiscard]] bool reserve(Device& device, rhi::Buffer& buffer, usize bytes);

    rhi::ShaderProgram* m_lineProgram = nullptr; // owned by the ShaderLibrary
    rhi::ShaderProgram* m_textProgram = nullptr;
    rhi::Pipeline m_linePipeline;
    rhi::Pipeline m_textPipeline;
    rhi::Buffer m_lineBuffer;
    rhi::Buffer m_textBuffer;
    rhi::Texture m_font;
    rhi::Sampler m_nearest;
    std::vector<u8> m_staging;
    std::vector<DebugGlyphQuad> m_quads;
};
} // namespace g7::render

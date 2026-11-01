#pragma once

// 2D drawing of the game UI (M14 part A): rectangles, images, 9-slice frames and distance-field text,
// collected per frame and drawn through the RHI in a few draw calls (shader ui.vert/ui.frag). Coordinates
// are framebuffer pixels with the origin top left, +Y down. Without a device it only collects (tests).

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/rhi/Resources.hpp>
#include <g7/ui/Style.hpp>
#include <g7/ui/Text.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace g7::render
{
class Device;
class ShaderLibrary;
} // namespace g7::render

namespace g7::ui
{
class Font;

struct Rect
{
    Vec2 min{0.0f};
    Vec2 max{0.0f};

    [[nodiscard]] static Rect fromSize(Vec2 position, Vec2 size) noexcept
    {
        return {position, position + size};
    }
    [[nodiscard]] f32 width() const noexcept { return max.x - min.x; }
    [[nodiscard]] f32 height() const noexcept { return max.y - min.y; }
    [[nodiscard]] Vec2 size() const noexcept { return max - min; }
    [[nodiscard]] bool empty() const noexcept { return max.x <= min.x || max.y <= min.y; }
    [[nodiscard]] Rect intersect(const Rect& other) const noexcept;
    friend bool operator==(const Rect&, const Rect&) = default;
};

/// A 9-slice frame ready to draw: the image (nullptr: plain colour), its size and the border widths in
/// image pixels; `borderScale` turns image pixels into framebuffer pixels for the corners.
struct NineSlice
{
    const render::rhi::Texture* texture = nullptr;
    Vec2 imageSize{0.0f};
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 right = 0.0f;
    f32 bottom = 0.0f;
    f32 borderScale = 1.0f;
};

/// How a text is drawn (sizes in framebuffer pixels).
struct TextStyle
{
    const Font* font = nullptr;
    Color color;
    f32 outline = 0.0f;
    Color outlineColor{0.0f, 0.0f, 0.0f, 1.0f};
    f32 shadow = 0.0f; ///< drop shadow offset in pixels (right and down); 0 = none
    Color shadowColor{0.0f, 0.0f, 0.0f, 0.6f};

    /// The same style with every colour's alpha multiplied (fading texts).
    [[nodiscard]] TextStyle faded(f32 opacity) const noexcept
    {
        TextStyle s = *this;
        s.color.a *= opacity;
        s.outlineColor.a *= opacity;
        s.shadowColor.a *= opacity;
        return s;
    }
};

class Canvas
{
public:
    /// The vertex layout of ui.vert (32 bytes).
    struct Vertex
    {
        Vec2 position{0.0f};
        Vec2 uv{0.0f};
        u32 color = 0;
        f32 pixelRange = 0.0f; ///< > 0: distance-field text with this screen range; 0: image (sRGB texels)
        f32 outline = 0.0f;    ///< outline width in pixels (text only)
        u32 outlineColor = 0;
    };
    /// One draw call: a texture (nullptr = white) and the clip rectangle in force.
    struct Command
    {
        const render::rhi::Texture* texture = nullptr;
        std::optional<Rect> clip;
        u32 firstIndex = 0;
        u32 indexCount = 0;
    };

    Canvas();
    ~Canvas();
    Canvas(Canvas&&) noexcept;
    Canvas& operator=(Canvas&&) noexcept;
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    /// Loads ui.vert/ui.frag and creates the white texture. Without a device (nullptr) the canvas only
    /// collects geometry (tests).
    [[nodiscard]] static Result<Canvas> create(render::Device* device, render::ShaderLibrary* shaders);

    /// Starts a frame for a framebuffer of `pixels`; `uiScale` = framebuffer pixels per UI unit.
    void begin(Vec2 pixels, f32 uiScale);
    [[nodiscard]] Vec2 pixels() const noexcept { return m_pixels; }
    [[nodiscard]] f32 uiScale() const noexcept { return m_uiScale; }

    void rect(const Rect& rect, Color color);
    /// An image; `uv` in texture coordinates (0..1, origin top left). The texture is sampled as sRGB.
    void image(const Rect& rect, const render::rhi::Texture* texture, const Rect& uv, Color tint,
               bool displayValues = false);
    /// A 9-slice frame stretched over `rect`; borders larger than the rectangle shrink proportionally.
    void frame(const Rect& rect, const NineSlice& slice, Color tint);
    /// A laid-out text with its box's top left at `position`.
    void text(Vec2 position, const TextLayout& layout, const TextStyle& style);

    /// Restricts later drawing to `clip` (intersected with the current clip).
    void pushClip(const Rect& clip);
    void popClip();

    /// Draws everything collected into the bound framebuffer (alpha blended, no depth) and clears the lists.
    void end(render::Device* device);

    // Inspection (tests): what the frame collected so far.
    [[nodiscard]] const std::vector<Vertex>& vertices() const noexcept { return m_vertices; }
    [[nodiscard]] const std::vector<u32>& indices() const noexcept { return m_indices; }
    [[nodiscard]] const std::vector<Command>& commands() const noexcept { return m_commands; }

private:
    void quad(const Rect& rect, const Rect& uv, const render::rhi::Texture* texture, u32 color,
              f32 pixelRange = 0.0f, f32 outline = 0.0f, u32 outlineColor = 0);
    std::optional<Rect> currentClip() const;

    struct Gpu;
    std::unique_ptr<Gpu> m_gpu;
    Vec2 m_pixels{0.0f};
    f32 m_uiScale = 1.0f;
    std::vector<Vertex> m_vertices;
    std::vector<u32> m_indices;
    std::vector<Command> m_commands;
    std::vector<Rect> m_clips;
};
} // namespace g7::ui

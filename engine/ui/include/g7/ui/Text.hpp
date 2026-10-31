#pragma once

// Text layout of the game UI (M14 part A): UTF-8, line breaks, kerning, alignment, truncation. Pure CPU -
// the result is drawn by Canvas::text and can be measured without a GPU.

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <string_view>
#include <vector>

namespace g7::ui
{
class Font;

enum class Align : u8
{
    Left,
    Center,
    Right,
};

struct TextOptions
{
    f32 size = 16.0f;     ///< pixels per em
    f32 wrapWidth = 0.0f; ///< pixels; 0 = no automatic breaks (only '\n')
    u32 maxLines = 0;     ///< 0 = unlimited; more lines are cut and the last ends in "…"
    Align align = Align::Left;
};

/// A glyph quad in pixels relative to the top left of the text box (+Y down), with its atlas coordinates.
struct PlacedGlyph
{
    Vec2 min{0.0f};
    Vec2 max{0.0f};
    Vec2 uvMin{0.0f};
    Vec2 uvMax{0.0f};
};

struct TextLayout
{
    std::vector<PlacedGlyph> glyphs; ///< only glyphs with an image (no spaces)
    Vec2 size{0.0f};                 ///< widest line (without trailing spaces) x lines * line height
    u32 lines = 0;
    bool truncated = false; ///< maxLines cut the text
    f32 pixelSize = 0.0f;   ///< TextOptions::size, for the shader's distance range
};

/// Lays out `utf8`. Lines break at '\n', and with a wrap width after spaces and hyphens; a word wider than
/// the line is broken between characters. Spaces at the end of a broken line are dropped, as are those at
/// the start of the next one. '\t' counts as a space, '\r' is ignored.
[[nodiscard]] TextLayout layoutText(const Font& font, std::string_view utf8, const TextOptions& options);

/// Width in pixels of `utf8` as one line (no breaks), with kerning.
[[nodiscard]] f32 measureText(const Font& font, std::string_view utf8, f32 size);
} // namespace g7::ui

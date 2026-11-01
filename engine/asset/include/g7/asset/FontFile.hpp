#pragma once

// Cooked font for the game UI (M14, ADR 0020): glyph metrics, kerning pairs and a distance-field atlas.
// g7-cook writes it (msdfgen + stb_truetype), the ui module reads it; no font rasterisation at run time.

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace g7::asset
{
enum class FontKind : u8
{
    Msdf = 0, ///< multi-channel signed distance field (RGB; median of the three = distance)
    Sdf = 1,  ///< single-channel signed distance field (in R)
};

/// One glyph. Plane bounds are in em (1 = the font size), relative to the pen position on the baseline,
/// +Y up; atlas bounds are in atlas pixels with the origin top left, +Y down. A glyph without outline
/// (space) has an empty atlas rectangle (width or height 0).
struct FontGlyph
{
    u32 codepoint = 0;
    f32 advance = 0.0f; ///< em
    f32 planeLeft = 0.0f;
    f32 planeBottom = 0.0f;
    f32 planeRight = 0.0f;
    f32 planeTop = 0.0f;
    u16 atlasX = 0;
    u16 atlasY = 0;
    u16 atlasWidth = 0;
    u16 atlasHeight = 0;

    [[nodiscard]] bool hasImage() const noexcept { return atlasWidth > 0 && atlasHeight > 0; }
};

/// Extra advance (em, usually negative) between two codepoints.
struct FontKerning
{
    u32 left = 0;
    u32 right = 0;
    f32 advance = 0.0f;
};

struct FontData
{
    FontKind kind = FontKind::Msdf;
    f32 emPixels = 0.0f;      ///< pixels per em at which the atlas was generated
    f32 distanceRange = 0.0f; ///< width of the distance field in atlas pixels (edge = 0.5)
    f32 ascender = 0.0f;      ///< em above the baseline
    f32 descender = 0.0f;     ///< em, negative below the baseline
    f32 lineGap = 0.0f;       ///< em between lines (line height = ascender - descender + lineGap)
    u32 atlasWidth = 0;
    u32 atlasHeight = 0;
    std::vector<u8> pixels;           ///< RGBA8, top row first, atlasWidth * atlasHeight * 4 bytes
    std::vector<FontGlyph> glyphs;    ///< sorted by codepoint, unique
    std::vector<FontKerning> kerning; ///< sorted by (left, right), unique
};

/// ".g7font" version 1, little-endian:
///
///     Header  magic "G7FN", version u32, kind u8, reserved u8 x 3, emPixels f32, distanceRange f32,
///             ascender f32, descender f32, lineGap f32, atlasWidth u32, atlasHeight u32,
///             glyphCount u32, kerningCount u32
///     Glyphs  codepoint u32, advance f32, plane l/b/r/t (4 x f32), atlas x/y/w/h (4 x u16)
///     Kerning left u32, right u32, advance f32
///     Atlas   compressed size u32, RGBA8 pixels compressed with zstd
inline constexpr char kFontMagic[4] = {'G', '7', 'F', 'N'};
inline constexpr u32 kFontVersion = 1;

[[nodiscard]] std::vector<u8> serializeFont(const FontData& font);
/// Parses and validates (sizes, sorted unique glyphs and pairs, atlas rectangles inside the atlas).
[[nodiscard]] Result<FontData> deserializeFont(std::span<const u8> bytes,
                                               std::string_view debugName = "<memory>");
} // namespace g7::asset

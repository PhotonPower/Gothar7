#include "ByteIo.hpp"

#include <g7/asset/FontFile.hpp>
#include <g7/core/Assert.hpp>

#include <zstd.h>

#include <string>

namespace g7::asset
{
namespace
{
using detail::ByteReader;
using detail::ByteWriter;

constexpr usize kGlyphSize = 4 + 4 + 4 * 4 + 4 * 2;
constexpr usize kKerningSize = 4 + 4 + 4;
constexpr u32 kMaxAtlasSide = 16384;

Error fontError(std::string_view debugName, const std::string& what)
{
    return Error{std::string(debugName) + ": " + what};
}
} // namespace

std::vector<u8> serializeFont(const FontData& font)
{
    G7_ASSERT(font.pixels.size() == static_cast<usize>(font.atlasWidth) * font.atlasHeight * 4,
              "atlas pixels must be RGBA8 of the atlas size");
    std::vector<u8> out;
    ByteWriter w(out);
    w.text(std::string_view(kFontMagic, sizeof(kFontMagic)));
    w.u32v(kFontVersion);
    w.u8v(static_cast<u8>(font.kind));
    w.u8v(0);
    w.u8v(0);
    w.u8v(0);
    w.f32v(font.emPixels);
    w.f32v(font.distanceRange);
    w.f32v(font.ascender);
    w.f32v(font.descender);
    w.f32v(font.lineGap);
    w.u32v(font.atlasWidth);
    w.u32v(font.atlasHeight);
    w.u32v(static_cast<u32>(font.glyphs.size()));
    w.u32v(static_cast<u32>(font.kerning.size()));
    for (const FontGlyph& g : font.glyphs)
    {
        w.u32v(g.codepoint);
        w.f32v(g.advance);
        w.f32v(g.planeLeft);
        w.f32v(g.planeBottom);
        w.f32v(g.planeRight);
        w.f32v(g.planeTop);
        w.u16v(g.atlasX);
        w.u16v(g.atlasY);
        w.u16v(g.atlasWidth);
        w.u16v(g.atlasHeight);
    }
    for (const FontKerning& k : font.kerning)
    {
        w.u32v(k.left);
        w.u32v(k.right);
        w.f32v(k.advance);
    }
    std::vector<u8> packed(ZSTD_compressBound(font.pixels.size()));
    const size_t written =
        ZSTD_compress(packed.data(), packed.size(), font.pixels.data(), font.pixels.size(), 19);
    G7_VERIFY(!ZSTD_isError(written), "zstd compression failed");
    packed.resize(written);
    w.blob32(packed);
    return out;
}

Result<FontData> deserializeFont(std::span<const u8> bytes, std::string_view debugName)
{
    ByteReader r(bytes);
    if (r.text(4) != std::string_view(kFontMagic, sizeof(kFontMagic)))
    {
        return fontError(debugName, "not a .g7font file");
    }
    const u32 version = r.u32v();
    if (version != kFontVersion)
    {
        return fontError(debugName, "unsupported .g7font version " + std::to_string(version));
    }
    FontData font;
    const u8 kind = r.u8v();
    (void)r.bytes(3);
    if (kind > static_cast<u8>(FontKind::Sdf))
    {
        return fontError(debugName, "unknown font kind " + std::to_string(kind));
    }
    font.kind = static_cast<FontKind>(kind);
    font.emPixels = r.f32v();
    font.distanceRange = r.f32v();
    font.ascender = r.f32v();
    font.descender = r.f32v();
    font.lineGap = r.f32v();
    font.atlasWidth = r.u32v();
    font.atlasHeight = r.u32v();
    const u32 glyphCount = r.u32v();
    const u32 kerningCount = r.u32v();
    if (r.failed())
    {
        return fontError(debugName, "truncated header");
    }
    if (font.atlasWidth == 0 || font.atlasHeight == 0 || font.atlasWidth > kMaxAtlasSide ||
        font.atlasHeight > kMaxAtlasSide || !(font.emPixels > 0.0f) || !(font.distanceRange > 0.0f))
    {
        return fontError(debugName, "invalid atlas size or metrics");
    }
    if (!r.has(static_cast<u64>(glyphCount) * kGlyphSize + static_cast<u64>(kerningCount) * kKerningSize))
    {
        return fontError(debugName, "truncated glyph or kerning table");
    }
    font.glyphs.resize(glyphCount);
    for (u32 i = 0; i < glyphCount; ++i)
    {
        FontGlyph& g = font.glyphs[i];
        g.codepoint = r.u32v();
        g.advance = r.f32v();
        g.planeLeft = r.f32v();
        g.planeBottom = r.f32v();
        g.planeRight = r.f32v();
        g.planeTop = r.f32v();
        g.atlasX = r.u16v();
        g.atlasY = r.u16v();
        g.atlasWidth = r.u16v();
        g.atlasHeight = r.u16v();
        if (i > 0 && font.glyphs[i - 1].codepoint >= g.codepoint)
        {
            return fontError(debugName, "glyphs not sorted by codepoint");
        }
        if (static_cast<u32>(g.atlasX) + g.atlasWidth > font.atlasWidth ||
            static_cast<u32>(g.atlasY) + g.atlasHeight > font.atlasHeight)
        {
            return fontError(debugName, "glyph " + std::to_string(g.codepoint) + " outside the atlas");
        }
    }
    font.kerning.resize(kerningCount);
    for (u32 i = 0; i < kerningCount; ++i)
    {
        FontKerning& k = font.kerning[i];
        k.left = r.u32v();
        k.right = r.u32v();
        k.advance = r.f32v();
        if (i > 0)
        {
            const FontKerning& p = font.kerning[i - 1];
            if (p.left > k.left || (p.left == k.left && p.right >= k.right))
            {
                return fontError(debugName, "kerning pairs not sorted");
            }
        }
    }
    const std::vector<u8> packed = r.blob32();
    if (r.failed())
    {
        return fontError(debugName, "truncated atlas");
    }
    const usize size = static_cast<usize>(font.atlasWidth) * font.atlasHeight * 4;
    font.pixels.resize(size);
    const size_t result = ZSTD_decompress(font.pixels.data(), size, packed.data(), packed.size());
    if (ZSTD_isError(result) || result != size)
    {
        return fontError(debugName, "corrupt atlas data");
    }
    return font;
}
} // namespace g7::asset

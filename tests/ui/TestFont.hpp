#pragma once

// A synthetic font for layout and canvas tests: every glyph 0.5 em wide, line height 1 em (ascender 0.8,
// descender -0.2), a 4x4 atlas cell per glyph, one kerning pair "AV" (-0.1 em). Space has no image.

#include <g7/asset/FontFile.hpp>
#include <g7/ui/Font.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace g7::ui::test
{
inline Font makeTestFont()
{
    asset::FontData data;
    data.emPixels = 32.0f;
    data.distanceRange = 4.0f;
    data.ascender = 0.8f;
    data.descender = -0.2f;
    data.lineGap = 0.0f;
    data.atlasWidth = 256;
    data.atlasHeight = 64;
    data.pixels.assign(static_cast<usize>(data.atlasWidth) * data.atlasHeight * 4, 0);
    std::vector<u32> codepoints{' ', '-', '?'};
    for (u32 c = 'A'; c <= 'Z'; ++c)
    {
        codepoints.push_back(c);
    }
    for (u32 c = 'a'; c <= 'z'; ++c)
    {
        codepoints.push_back(c);
    }
    codepoints.push_back(0xE4);   // ä
    codepoints.push_back(0x2026); // …
    std::sort(codepoints.begin(), codepoints.end());
    u16 cell = 0;
    for (const u32 c : codepoints)
    {
        asset::FontGlyph g;
        g.codepoint = c;
        g.advance = 0.5f;
        if (c != ' ')
        {
            g.planeLeft = 0.0f;
            g.planeBottom = -0.1f;
            g.planeRight = 0.5f;
            g.planeTop = 0.7f;
            g.atlasX = static_cast<u16>((cell % 64) * 4);
            g.atlasY = static_cast<u16>((cell / 64) * 4);
            g.atlasWidth = 4;
            g.atlasHeight = 4;
            ++cell;
        }
        data.glyphs.push_back(g);
    }
    data.kerning.push_back({'A', 'V', -0.1f});
    auto font = Font::fromData(std::move(data), "test-font");
    REQUIRE(font);
    return std::move(font).value();
}
} // namespace g7::ui::test

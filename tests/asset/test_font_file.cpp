// ".g7font" (M14 part A, ADR 0020): round trip and validation.

#include <g7/asset/FontFile.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
FontData sampleFont()
{
    FontData font;
    font.kind = FontKind::Msdf;
    font.emPixels = 48.0f;
    font.distanceRange = 6.0f;
    font.ascender = 0.9f;
    font.descender = -0.25f;
    font.lineGap = 0.05f;
    font.atlasWidth = 8;
    font.atlasHeight = 4;
    font.pixels.resize(8 * 4 * 4);
    for (usize i = 0; i < font.pixels.size(); ++i)
    {
        font.pixels[i] = static_cast<u8>(i * 7);
    }
    font.glyphs.push_back({' ', 0.25f});
    font.glyphs.push_back({'A', 0.6f, -0.05f, -0.1f, 0.65f, 0.75f, 0, 0, 4, 4});
    font.glyphs.push_back({0xE4, 0.5f, 0.0f, -0.1f, 0.5f, 0.8f, 4, 0, 4, 4});
    font.kerning.push_back({'A', 'V', -0.08f});
    font.kerning.push_back({'V', 'A', -0.07f});
    return font;
}
} // namespace

TEST_CASE("FontFile: round trip")
{
    const FontData font = sampleFont();
    const std::vector<u8> bytes = serializeFont(font);
    auto read = deserializeFont(bytes, "sample.g7font");
    REQUIRE_MESSAGE(read, (read ? "" : read.error().message));
    const FontData& r = read.value();
    CHECK(r.kind == FontKind::Msdf);
    CHECK(r.emPixels == 48.0f);
    CHECK(r.distanceRange == 6.0f);
    CHECK(r.ascender == 0.9f);
    CHECK(r.descender == -0.25f);
    CHECK(r.lineGap == 0.05f);
    CHECK(r.pixels == font.pixels);
    REQUIRE(r.glyphs.size() == 3);
    CHECK(r.glyphs[1].codepoint == 'A');
    CHECK(r.glyphs[1].planeTop == 0.75f);
    CHECK(r.glyphs[2].atlasX == 4);
    CHECK_FALSE(r.glyphs[0].hasImage());
    REQUIRE(r.kerning.size() == 2);
    CHECK(r.kerning[1].advance == -0.07f);
}

TEST_CASE("FontFile: invalid files are rejected")
{
    CHECK_FALSE(deserializeFont(std::vector<u8>{'G', '7'}));
    std::vector<u8> bytes = serializeFont(sampleFont());
    bytes[0] = 'X';
    CHECK(deserializeFont(bytes).error().message.find("not a .g7font") != std::string::npos);

    FontData unsorted = sampleFont();
    std::swap(unsorted.glyphs[1], unsorted.glyphs[2]);
    CHECK_FALSE(deserializeFont(serializeFont(unsorted)));

    FontData outside = sampleFont();
    outside.glyphs[2].atlasX = 6; // 6 + 4 > 8
    CHECK(deserializeFont(serializeFont(outside)).error().message.find("outside the atlas") !=
          std::string::npos);

    FontData pairs = sampleFont();
    std::swap(pairs.kerning[0], pairs.kerning[1]);
    CHECK_FALSE(deserializeFont(serializeFont(pairs)));

    std::vector<u8> truncated = serializeFont(sampleFont());
    truncated.resize(truncated.size() - 3);
    CHECK_FALSE(deserializeFont(truncated));
}

// Font cooking (M14 part A, ADR 0020): descriptions, character sets, the MSDF atlas of a real font
// (Alegreya, assets/source/fonts) and the round trip through ".g7font".

#include "../asset/TempDir.hpp"
#include "Cooker.hpp"
#include "Fonts.hpp"

#include <g7/asset/FontFile.hpp>
#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

using namespace g7;

namespace
{
const fs::Path kFontDir = fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "fonts";

/// Median of the three channels at atlas pixel (x, y): > 0.5 inside the outline.
f32 sampleMedian(const asset::FontData& font, u32 x, u32 y)
{
    const u8* p = &font.pixels[(static_cast<usize>(y) * font.atlasWidth + x) * 4];
    const u8 r = p[0];
    const u8 g = p[1];
    const u8 b = p[2];
    return static_cast<f32>(std::max(std::min(r, g), std::min(std::max(r, g), b))) / 255.0f;
}

const asset::FontGlyph* findGlyph(const asset::FontData& font, u32 codepoint)
{
    for (const asset::FontGlyph& g : font.glyphs)
    {
        if (g.codepoint == codepoint)
        {
            return &g;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("Fonts: character sets")
{
    CHECK(cook::charsetCodepoints("ascii").size() == 95);
    CHECK(cook::charsetCodepoints("latin1").size() == 96);
    const auto typography = cook::charsetCodepoints("typography");
    CHECK(std::find(typography.begin(), typography.end(), 0x201Eu) != typography.end()); // „
    CHECK(std::find(typography.begin(), typography.end(), 0x2026u) != typography.end()); // …
    CHECK(cook::charsetCodepoints("klingon").empty());
}

TEST_CASE("Fonts: descriptions")
{
    auto spec = cook::parseFontSpec("version = 1\nsource = \"a.ttf\"\nsize = 40\nrange = 4\n"
                                    "charset = [\"ascii\"]\nextra = \"\xE2\x82\xAC\xC3\xA4\"\n");
    REQUIRE_MESSAGE(spec, (spec ? "" : spec.error().message));
    CHECK(spec.value().source == "a.ttf");
    CHECK(spec.value().kind == asset::FontKind::Msdf);
    CHECK(spec.value().emPixels == 40.0f);
    CHECK(spec.value().distanceRange == 4.0f);
    CHECK(spec.value().codepoints.size() == 95 + 2); // ascii + € + ä
    CHECK(std::is_sorted(spec.value().codepoints.begin(), spec.value().codepoints.end()));
    CHECK(spec.value().codepoints.back() == 0x20ACu);

    CHECK_FALSE(cook::parseFontSpec("version = 2\nsource = \"a.ttf\"\n"));
    CHECK_FALSE(cook::parseFontSpec("version = 1\n"));                                   // no source
    CHECK_FALSE(cook::parseFontSpec("version = 1\nsource = \"a.ttf\"\nkind = \"x\"\n")); // unknown kind
    CHECK_FALSE(cook::parseFontSpec("version = 1\nsource = \"a.ttf\"\nsize = 4\n"));     // too small
    CHECK_FALSE(cook::parseFontSpec("version = 1\nsource = \"a.ttf\"\nrange = 40\n"));   // over half the size
    CHECK_FALSE(cook::parseFontSpec("version = 1\nsource = \"a.ttf\"\ncharset = [\"x\"]\n"));
}

TEST_CASE("Fonts: the shipped descriptions parse")
{
    for (const char* name : {"alegreya.font.toml", "alegreya_sc.font.toml"})
    {
        auto text = fs::readText(kFontDir / name);
        REQUIRE(text);
        auto spec = cook::parseFontSpec(text.value(), name);
        REQUIRE_MESSAGE(spec, (spec ? "" : spec.error().message));
        CHECK(fs::exists(kFontDir / fs::fromUtf8(spec.value().source)));
    }
}

TEST_CASE("Fonts: font sources stay out of the cooked output")
{
    test::TempDir dir;
    const fs::Path source = dir.path() / "source";
    REQUIRE(fs::createDirectories(source / "fonts"));
    REQUIRE(fs::writeText(source / "fonts" / "a.font.toml", "version = 1\nsource = \"a.ttf\"\n"));
    REQUIRE(fs::writeText(source / "fonts" / "a.ttf", "not really a font"));
    REQUIRE(fs::writeText(source / "fonts" / "a.g7font", "generated"));
    auto report = cook::cook({.source = source, .out = dir.path() / "out"});
    REQUIRE(report);
    CHECK(report.value().skipped == 2);
    CHECK(report.value().copied == 1);
    CHECK(fs::exists(dir.path() / "out" / "fonts" / "a.g7font"));
    CHECK_FALSE(fs::exists(dir.path() / "out" / "fonts" / "a.ttf"));
}

TEST_CASE("Fonts: an MSDF atlas of Alegreya")
{
    auto bytes = fs::readFile(kFontDir / "Alegreya-Regular.ttf");
    REQUIRE(bytes);
    cook::FontSpec spec;
    spec.source = "Alegreya-Regular.ttf";
    spec.emPixels = 32.0f;
    spec.distanceRange = 4.0f;
    spec.codepoints = {' ', 'A', 'V', 'l', 'o', 0xE4, 0x2026, 0x10FFFF}; // the last is not in the font
    auto built = cook::buildFont(bytes.value(), spec, "Alegreya-Regular.ttf");
    if (!cook::fontCookingAvailable())
    {
        CHECK_FALSE(built);
        return;
    }
    REQUIRE_MESSAGE(built, (built ? "" : built.error().message));
    const asset::FontData& font = built.value();
    CHECK(font.glyphs.size() == 7); // the missing character is left out
    CHECK(font.ascender > 0.5f);
    CHECK(font.descender < 0.0f);
    CHECK(font.atlasWidth == 1024);

    const asset::FontGlyph* space = findGlyph(font, ' ');
    REQUIRE(space != nullptr);
    CHECK_FALSE(space->hasImage());
    CHECK(space->advance > 0.1f);

    // The stem of 'l' is inside, the counter of 'o' outside, the cell corners outside.
    const asset::FontGlyph* l = findGlyph(font, 'l');
    const asset::FontGlyph* o = findGlyph(font, 'o');
    REQUIRE(l != nullptr);
    REQUIRE(o != nullptr);
    REQUIRE(l->hasImage());
    CHECK(sampleMedian(font, l->atlasX + l->atlasWidth / 2u, l->atlasY + l->atlasHeight / 2u) > 0.5f);
    CHECK(sampleMedian(font, o->atlasX + o->atlasWidth / 2u, o->atlasY + o->atlasHeight / 2u) < 0.5f);
    CHECK(sampleMedian(font, o->atlasX, o->atlasY) < 0.5f);
    // Plane bounds: 'l' rises above the x-height of 'o'.
    CHECK(l->planeTop > o->planeTop);

    // Alegreya kerns "AV".
    const bool kernsAV = std::any_of(font.kerning.begin(), font.kerning.end(), [](const asset::FontKerning& k)
                                     { return k.left == 'A' && k.right == 'V' && k.advance < 0.0f; });
    CHECK(kernsAV);

    // Deterministic, and the file round trip keeps everything.
    auto again = cook::buildFont(bytes.value(), spec, "Alegreya-Regular.ttf");
    REQUIRE(again);
    const std::vector<u8> file = asset::serializeFont(font);
    CHECK(asset::serializeFont(again.value()) == file);
    auto read = asset::deserializeFont(file, "test.g7font");
    REQUIRE_MESSAGE(read, (read ? "" : read.error().message));
    CHECK(read.value().pixels == font.pixels);
    CHECK(read.value().glyphs.size() == font.glyphs.size());
    CHECK(read.value().kerning.size() == font.kerning.size());
}

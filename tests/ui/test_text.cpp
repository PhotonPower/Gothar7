// Text layout without a GPU (M14 part A): breaks, kerning, alignment, truncation, missing glyphs.

#include "TestFont.hpp"

#include <g7/ui/Text.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::ui;

TEST_CASE("Text: one line, advances and kerning")
{
    const Font font = test::makeTestFont();
    CHECK(measureText(font, "abc", 20.0f) == doctest::Approx(30.0f)); // 3 x 0.5 em x 20 px
    CHECK(measureText(font, "AV", 20.0f) == doctest::Approx(18.0f));  // 2 x 10 px - 2 px kerning
    CHECK(measureText(font, "VA", 20.0f) == doctest::Approx(20.0f));  // the pair is ordered

    const TextLayout layout = layoutText(font, "a b", {.size = 20.0f});
    CHECK(layout.lines == 1);
    CHECK(layout.glyphs.size() == 2); // the space has no image
    CHECK(layout.size.x == doctest::Approx(30.0f));
    CHECK(layout.size.y == doctest::Approx(20.0f)); // one line of 1 em
    // Glyph box: plane bounds around the baseline at the ascender (0.8 em = 16 px).
    CHECK(layout.glyphs[0].min.x == doctest::Approx(0.0f));
    CHECK(layout.glyphs[0].min.y == doctest::Approx(16.0f - 0.7f * 20.0f));
    CHECK(layout.glyphs[0].max.y == doctest::Approx(16.0f + 0.1f * 20.0f));
    CHECK(layout.glyphs[1].min.x == doctest::Approx(20.0f));
    CHECK(layout.pixelSize == 20.0f);
}

TEST_CASE("Text: UTF-8, empty text and missing glyphs")
{
    const Font font = test::makeTestFont();
    CHECK(layoutText(font, "", {.size = 10.0f}).lines == 0);
    // ä is in the font (one glyph from two bytes); ß is not and falls back to '?'.
    const TextLayout umlaut = layoutText(font, "\xC3\xA4", {.size = 10.0f});
    CHECK(umlaut.glyphs.size() == 1);
    const TextLayout missing = layoutText(font, "\xC3\x9F", {.size = 10.0f});
    REQUIRE(missing.glyphs.size() == 1);
    const asset::FontGlyph& question = *font.find('?');
    CHECK(missing.glyphs[0].uvMin.x == doctest::Approx(question.atlasX / 256.0f));
    CHECK(font.find(0xDF) == nullptr);
}

TEST_CASE("Text: hard and automatic line breaks")
{
    const Font font = test::makeTestFont();
    // Size 10: each character 5 px. "aaa bbb" is 35 px; a 20 px line holds 4 characters.
    const TextLayout hard = layoutText(font, "ab\ncd", {.size = 10.0f});
    CHECK(hard.lines == 2);
    CHECK(hard.size.y == doctest::Approx(20.0f));

    const TextLayout wrapped = layoutText(font, "aaa bbb", {.size = 10.0f, .wrapWidth = 20.0f});
    CHECK(wrapped.lines == 2);
    CHECK(wrapped.size.x == doctest::Approx(15.0f)); // the break space is dropped
    REQUIRE(wrapped.glyphs.size() == 6);
    CHECK(wrapped.glyphs[3].min.x == doctest::Approx(0.0f)); // "bbb" starts the second line
    CHECK(wrapped.glyphs[3].min.y > wrapped.glyphs[0].min.y);

    // A word longer than the line is broken between characters.
    const TextLayout longWord = layoutText(font, "aaaaaaaaa", {.size = 10.0f, .wrapWidth = 20.0f});
    CHECK(longWord.lines == 3); // 4 + 4 + 1

    // Breaks after a hyphen.
    const TextLayout hyphen = layoutText(font, "ab-cdef", {.size = 10.0f, .wrapWidth = 20.0f});
    CHECK(hyphen.lines == 2);
    CHECK(hyphen.size.x == doctest::Approx(20.0f)); // "cdef"

    // Leading spaces after an automatic break vanish, those after '\n' stay.
    const TextLayout spaces = layoutText(font, "aaa   bb", {.size = 10.0f, .wrapWidth = 20.0f});
    CHECK(spaces.lines == 2);
    CHECK(spaces.glyphs[3].min.x == doctest::Approx(0.0f));
    const TextLayout indent = layoutText(font, "a\n  b", {.size = 10.0f});
    CHECK(indent.glyphs[1].min.x == doctest::Approx(10.0f));
}

TEST_CASE("Text: alignment within the wrap width")
{
    const Font font = test::makeTestFont();
    const TextLayout center =
        layoutText(font, "ab", {.size = 10.0f, .wrapWidth = 40.0f, .align = Align::Center});
    CHECK(center.glyphs[0].min.x == doctest::Approx(15.0f)); // (40 - 10) / 2
    const TextLayout right =
        layoutText(font, "ab", {.size = 10.0f, .wrapWidth = 40.0f, .align = Align::Right});
    CHECK(right.glyphs[0].min.x == doctest::Approx(30.0f));
    // Without a wrap width, lines align within the widest line.
    const TextLayout block = layoutText(font, "abcd\nab", {.size = 10.0f, .align = Align::Right});
    CHECK(block.glyphs[4].min.x == doctest::Approx(10.0f));
}

TEST_CASE("Text: truncation with an ellipsis")
{
    const Font font = test::makeTestFont();
    const TextLayout cut =
        layoutText(font, "aaaa bbbb cccc", {.size = 10.0f, .wrapWidth = 20.0f, .maxLines = 2});
    CHECK(cut.truncated);
    CHECK(cut.lines == 2);
    // "bbbb" plus "…" would be 25 px: one b goes, the ellipsis ends the line within 20 px.
    CHECK(cut.size.x <= 20.0f + 1e-4f);
    CHECK(cut.glyphs.size() == 4 + 4);
    const asset::FontGlyph& ellipsis = *font.find(0x2026);
    CHECK(cut.glyphs.back().uvMin.x == doctest::Approx(ellipsis.atlasX / 256.0f));
    CHECK_FALSE(layoutText(font, "aaaa", {.size = 10.0f, .wrapWidth = 20.0f, .maxLines = 2}).truncated);
}

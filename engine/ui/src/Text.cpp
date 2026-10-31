#include <g7/core/StringUtil.hpp>
#include <g7/ui/Font.hpp>
#include <g7/ui/Text.hpp>

#include <algorithm>
#include <span>

namespace g7::ui
{
namespace
{
constexpr u32 kEllipsis = 0x2026;

bool isBreakAfter(u32 c) noexcept
{
    return c == '-' || c == 0x2013 || c == 0x2014; // hyphen, en dash, em dash
}

std::vector<u32> decode(std::string_view utf8)
{
    std::vector<u32> out;
    out.reserve(utf8.size());
    for (usize pos = 0; pos < utf8.size();)
    {
        const u32 c = nextCodepoint(utf8, pos);
        if (c == '\r')
        {
            continue;
        }
        out.push_back(c == '\t' ? static_cast<u32>(' ') : c);
    }
    return out;
}

/// Width in em of a run of codepoints (advances plus kerning).
f32 widthOf(const Font& font, std::span<const u32> run)
{
    f32 width = 0.0f;
    for (usize i = 0; i < run.size(); ++i)
    {
        if (i > 0)
        {
            width += font.kerning(run[i - 1], run[i]);
        }
        width += font.glyph(run[i]).advance;
    }
    return width;
}

void trimTrailingSpaces(std::vector<u32>& line)
{
    while (!line.empty() && line.back() == ' ')
    {
        line.pop_back();
    }
}

std::vector<std::vector<u32>> breakLines(const Font& font, const std::vector<u32>& text, f32 wrapEm)
{
    std::vector<std::vector<u32>> lines;
    usize lineStart = 0;
    usize i = 0;
    usize lastBreak = 0; // 0 = none on this line (a break is never at the line start)
    f32 pen = 0.0f;
    bool hasPrevious = false;
    const auto finish = [&](usize end)
    {
        std::vector<u32> line(text.begin() + static_cast<std::ptrdiff_t>(lineStart),
                              text.begin() + static_cast<std::ptrdiff_t>(end));
        trimTrailingSpaces(line);
        lines.push_back(std::move(line));
    };
    const auto restart = [&](usize start)
    {
        lineStart = start;
        while (lineStart < text.size() && text[lineStart] == ' ')
        {
            ++lineStart; // spaces after an automatic break vanish
        }
        i = lineStart;
        lastBreak = 0;
        pen = 0.0f;
        hasPrevious = false;
    };
    while (i < text.size())
    {
        const u32 c = text[i];
        if (c == '\n')
        {
            finish(i);
            lineStart = i + 1; // a hard break keeps the next line's leading spaces
            i = lineStart;
            lastBreak = 0;
            pen = 0.0f;
            hasPrevious = false;
            continue;
        }
        const f32 advance = (hasPrevious ? font.kerning(text[i - 1], c) : 0.0f) + font.glyph(c).advance;
        if (wrapEm > 0.0f && c != ' ' && i > lineStart && pen + advance > wrapEm)
        {
            if (lastBreak > lineStart)
            {
                finish(lastBreak);
                restart(lastBreak);
            }
            else
            {
                finish(i); // a word wider than the line: break between characters
                restart(i);
            }
            continue;
        }
        pen += advance;
        hasPrevious = true;
        if (c == ' ' || (isBreakAfter(c) && i > lineStart))
        {
            lastBreak = i + 1;
        }
        ++i;
    }
    finish(text.size());
    return lines;
}
} // namespace

TextLayout layoutText(const Font& font, std::string_view utf8, const TextOptions& options)
{
    TextLayout layout;
    layout.pixelSize = options.size;
    const f32 size = options.size;
    const f32 wrapEm = options.wrapWidth > 0.0f && size > 0.0f ? options.wrapWidth / size : 0.0f;
    const std::vector<u32> text = decode(utf8);
    if (text.empty())
    {
        return layout; // no lines
    }
    std::vector<std::vector<u32>> lines = breakLines(font, text, wrapEm);

    if (options.maxLines > 0 && lines.size() > options.maxLines)
    {
        lines.resize(options.maxLines);
        layout.truncated = true;
        std::vector<u32>& last = lines.back();
        last.push_back(kEllipsis);
        while (wrapEm > 0.0f && last.size() > 1 && widthOf(font, last) > wrapEm)
        {
            last.erase(last.end() - 2);
            while (last.size() > 1 && last[last.size() - 2] == ' ')
            {
                last.erase(last.end() - 2);
            }
        }
    }

    std::vector<f32> widths;
    widths.reserve(lines.size());
    f32 widest = 0.0f;
    for (const auto& line : lines)
    {
        widths.push_back(widthOf(font, line) * size);
        widest = std::max(widest, widths.back());
    }
    const f32 boxWidth = options.wrapWidth > 0.0f ? options.wrapWidth : widest;
    const f32 lineHeight = font.lineHeight() * size;
    const asset::FontData& data = font.data();
    const Vec2 atlas(static_cast<f32>(data.atlasWidth), static_cast<f32>(data.atlasHeight));

    for (usize l = 0; l < lines.size(); ++l)
    {
        const auto& line = lines[l];
        f32 x = 0.0f;
        if (options.align == Align::Center)
        {
            x = (boxWidth - widths[l]) * 0.5f;
        }
        else if (options.align == Align::Right)
        {
            x = boxWidth - widths[l];
        }
        const f32 baseline = static_cast<f32>(l) * lineHeight + font.ascender() * size;
        for (usize i = 0; i < line.size(); ++i)
        {
            if (i > 0)
            {
                x += font.kerning(line[i - 1], line[i]) * size;
            }
            const asset::FontGlyph& g = font.glyph(line[i]);
            if (g.hasImage())
            {
                PlacedGlyph placed;
                placed.min = Vec2(x + g.planeLeft * size, baseline - g.planeTop * size);
                placed.max = Vec2(x + g.planeRight * size, baseline - g.planeBottom * size);
                const Vec2 origin(static_cast<f32>(g.atlasX), static_cast<f32>(g.atlasY));
                placed.uvMin = origin / atlas;
                placed.uvMax =
                    (origin + Vec2(static_cast<f32>(g.atlasWidth), static_cast<f32>(g.atlasHeight))) / atlas;
                layout.glyphs.push_back(placed);
            }
            x += g.advance * size;
        }
    }
    layout.lines = static_cast<u32>(lines.size());
    layout.size = Vec2(widest, static_cast<f32>(lines.size()) * lineHeight);
    return layout;
}

f32 measureText(const Font& font, std::string_view utf8, f32 size)
{
    std::vector<u32> text = decode(utf8);
    std::erase(text, static_cast<u32>('\n'));
    return widthOf(font, text) * size;
}
} // namespace g7::ui

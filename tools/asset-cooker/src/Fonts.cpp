#include "Fonts.hpp"

#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <set>
#include <utility>

#define STB_TRUETYPE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION
#define STBTT_STATIC
#define STBRP_STATIC
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wduplicated-branches"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include <stb_rect_pack.h>
#include <stb_truetype.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if G7_HAS_MSDFGEN
#include <core/ShapeDistanceFinder.h>
#include <msdfgen.h>
#endif

namespace g7::cook
{
namespace
{
constexpr u32 kAtlasWidth = 1024;
constexpr u32 kMaxAtlasHeight = 8192;
constexpr int kPadding = 1; // empty pixels between glyphs (linear filtering)

void addRange(std::vector<u32>& out, u32 first, u32 last)
{
    for (u32 c = first; c <= last; ++c)
    {
        out.push_back(c);
    }
}

Error specError(std::string_view debugName, const std::string& what)
{
    return Error{std::string(debugName) + ": " + what};
}

#if G7_HAS_MSDFGEN
struct GlyphJob
{
    u32 codepoint = 0;
    int index = 0; // glyph index in the font
    f32 advance = 0.0f;
    msdfgen::Shape shape;
    bool empty = true;
    int left = 0; // pixel box in the em grid (y up)
    int bottom = 0;
    int width = 0;
    int height = 0;
};

/// The glyph outline in em (y up), or an empty shape.
msdfgen::Shape glyphShape(const stbtt_fontinfo& info, int glyph, double unitsToEm)
{
    msdfgen::Shape shape;
    stbtt_vertex* vertices = nullptr;
    const int count = stbtt_GetGlyphShape(&info, glyph, &vertices);
    msdfgen::Contour* contour = nullptr;
    msdfgen::Point2 last;
    const auto point = [unitsToEm](double x, double y)
    { return msdfgen::Point2(x * unitsToEm, y * unitsToEm); };
    for (int i = 0; i < count; ++i)
    {
        const stbtt_vertex& v = vertices[i];
        const msdfgen::Point2 p = point(v.x, v.y);
        switch (v.type)
        {
        case STBTT_vmove:
            contour = &shape.addContour();
            break;
        case STBTT_vline:
            if (contour != nullptr && p != last)
            {
                contour->addEdge(msdfgen::EdgeHolder(last, p));
            }
            break;
        case STBTT_vcurve:
            if (contour != nullptr && p != last)
            {
                contour->addEdge(msdfgen::EdgeHolder(last, point(v.cx, v.cy), p));
            }
            break;
        case STBTT_vcubic:
            if (contour != nullptr && p != last)
            {
                contour->addEdge(msdfgen::EdgeHolder(last, point(v.cx, v.cy), point(v.cx1, v.cy1), p));
            }
            break;
        default:
            break;
        }
        last = p;
    }
    stbtt_FreeShape(&info, vertices);
    // Drop contours without edges (a lone move).
    std::erase_if(shape.contours, [](const msdfgen::Contour& c) { return c.edges.empty(); });
    return shape;
}
#endif
} // namespace

std::vector<u32> charsetCodepoints(std::string_view name)
{
    std::vector<u32> out;
    if (name == "ascii")
    {
        addRange(out, 0x20, 0x7E);
    }
    else if (name == "latin1")
    {
        addRange(out, 0xA0, 0xFF);
    }
    else if (name == "typography")
    {
        // en/em dash, single and double quotes (German and English), bullet, ellipsis, guillemets, euro.
        out = {0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D,
               0x201E, 0x2022, 0x2026, 0x2039, 0x203A, 0x20AC};
    }
    return out;
}

Result<FontSpec> parseFontSpec(std::string_view toml, std::string_view debugName)
{
    auto parsed = Config::parse(toml, debugName);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& config = parsed.value();
    if (config.get<i64>("version", 0) != 1)
    {
        return specError(debugName, "version must be 1");
    }
    FontSpec spec;
    spec.source = config.get<std::string>("source", "");
    if (spec.source.empty())
    {
        return specError(debugName, "'source' (the font file) is missing");
    }
    const std::string kind = config.get<std::string>("kind", "msdf");
    if (kind == "msdf")
    {
        spec.kind = asset::FontKind::Msdf;
    }
    else if (kind == "sdf")
    {
        spec.kind = asset::FontKind::Sdf;
    }
    else
    {
        return specError(debugName, "unknown kind '" + kind + "' (msdf or sdf)");
    }
    spec.emPixels = static_cast<f32>(config.get<f64>("size", 48.0));
    spec.distanceRange = static_cast<f32>(config.get<f64>("range", 6.0));
    if (spec.emPixels < 8.0f || spec.emPixels > 256.0f)
    {
        return specError(debugName, "size must be 8..256 pixels per em");
    }
    if (spec.distanceRange < 1.0f || spec.distanceRange > spec.emPixels / 2.0f)
    {
        return specError(debugName, "range must be at least 1 and at most half the size");
    }
    std::set<u32> codepoints;
    for (const std::string& set : config.get<std::vector<std::string>>("charset", {"ascii"}))
    {
        const auto members = charsetCodepoints(set);
        if (members.empty())
        {
            return specError(debugName, "unknown charset '" + set + "' (ascii, latin1, typography)");
        }
        codepoints.insert(members.begin(), members.end());
    }
    const std::string extra = config.get<std::string>("extra", "");
    for (usize pos = 0; pos < extra.size();)
    {
        codepoints.insert(nextCodepoint(extra, pos));
    }
    spec.codepoints.assign(codepoints.begin(), codepoints.end());
    return spec;
}

bool fontCookingAvailable() noexcept
{
    return G7_HAS_MSDFGEN != 0;
}

Result<asset::FontData> buildFont(std::span<const u8> fontFile, const FontSpec& spec,
                                  std::string_view debugName)
{
#if !G7_HAS_MSDFGEN
    (void)fontFile;
    (void)spec;
    return specError(debugName, "this g7-cook was built without msdfgen (ADR 0020) - fonts cannot be cooked");
#else
    stbtt_fontinfo info{};
    const int offset = stbtt_GetFontOffsetForIndex(fontFile.data(), 0);
    if (offset < 0 || stbtt_InitFont(&info, fontFile.data(), offset) == 0)
    {
        return specError(debugName, "not a TrueType/OpenType font stb_truetype can read");
    }
    const double unitsToEm = static_cast<double>(stbtt_ScaleForMappingEmToPixels(&info, 1.0f));
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    asset::FontData font;
    font.kind = spec.kind;
    font.emPixels = spec.emPixels;
    font.distanceRange = spec.distanceRange;
    font.ascender = static_cast<f32>(ascent * unitsToEm);
    font.descender = static_cast<f32>(descent * unitsToEm);
    font.lineGap = static_cast<f32>(lineGap * unitsToEm);

    const double em = spec.emPixels;
    const double pad = spec.distanceRange / 2.0; // pixels around the outline
    std::vector<GlyphJob> jobs;
    std::vector<u32> missing;
    for (const u32 codepoint : spec.codepoints)
    {
        const int index = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
        if (index == 0 && codepoint != 0)
        {
            missing.push_back(codepoint);
            continue;
        }
        GlyphJob job;
        job.codepoint = codepoint;
        job.index = index;
        int advance = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&info, index, &advance, &bearing);
        job.advance = static_cast<f32>(advance * unitsToEm);
        job.shape = glyphShape(info, index, unitsToEm);
        if (!job.shape.contours.empty())
        {
            job.shape.normalize();
            const auto b = job.shape.getBounds();
            // Outlines wound the other way round (as msdfgen sees it): a point far outside must be outside.
            const msdfgen::Point2 outside(b.l - (b.r - b.l) - 1.0, b.b - (b.t - b.b) - 1.0);
            if (msdfgen::SimpleTrueShapeDistanceFinder::oneShotDistance(job.shape, outside) > 0.0)
            {
                for (msdfgen::Contour& contour : job.shape.contours)
                {
                    contour.reverse();
                }
            }
            job.left = static_cast<int>(std::floor(b.l * em - pad));
            job.bottom = static_cast<int>(std::floor(b.b * em - pad));
            job.width = static_cast<int>(std::ceil(b.r * em + pad)) - job.left;
            job.height = static_cast<int>(std::ceil(b.t * em + pad)) - job.bottom;
            job.empty = job.width <= 0 || job.height <= 0;
        }
        jobs.push_back(std::move(job));
    }
    if (jobs.empty())
    {
        return specError(debugName, "the font has none of the requested characters");
    }
    if (!missing.empty())
    {
        std::string list;
        for (const u32 c : missing)
        {
            list += std::format("{}U+{:04X}", list.empty() ? "" : " ", c);
        }
        G7_LOG_WARN("cook", "{}: {} characters not in the font, left out: {}", debugName, missing.size(),
                    list);
    }

    // Pack: fixed width, the smallest power-of-two height that fits.
    std::vector<stbrp_rect> rects;
    for (usize i = 0; i < jobs.size(); ++i)
    {
        if (!jobs[i].empty)
        {
            stbrp_rect r{};
            r.id = static_cast<int>(i);
            r.w = jobs[i].width + kPadding;
            r.h = jobs[i].height + kPadding;
            rects.push_back(r);
        }
    }
    u32 height = 64;
    for (;; height *= 2)
    {
        if (height > kMaxAtlasHeight)
        {
            return specError(debugName, "glyphs do not fit into a 1024 x 8192 atlas - lower 'size'");
        }
        std::vector<stbrp_node> nodes(kAtlasWidth);
        stbrp_context context{};
        stbrp_init_target(&context, static_cast<int>(kAtlasWidth), static_cast<int>(height), nodes.data(),
                          static_cast<int>(nodes.size()));
        for (stbrp_rect& r : rects)
        {
            r.was_packed = 0;
        }
        if (stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size())) == 1)
        {
            break;
        }
    }
    font.atlasWidth = kAtlasWidth;
    font.atlasHeight = height;
    font.pixels.assign(static_cast<usize>(kAtlasWidth) * height * 4, 0);
    for (usize p = 3; p < font.pixels.size(); p += 4)
    {
        font.pixels[p] = 255;
    }

    const msdfgen::DistanceMapping mapping(msdfgen::Range(spec.distanceRange / em));
    for (const stbrp_rect& r : rects)
    {
        GlyphJob& job = jobs[static_cast<usize>(r.id)];
        const msdfgen::Projection projection(msdfgen::Vector2(em),
                                             msdfgen::Vector2(-job.left / em, -job.bottom / em));
        const msdfgen::SDFTransformation transformation(projection, mapping);
        const auto write = [&](int x, int y, const float* v, int channels)
        {
            // The bitmap's row 0 is the bottom (y up); the atlas stores the top row first.
            const usize ax = static_cast<usize>(r.x + x);
            const usize ay = static_cast<usize>(r.y + (job.height - 1 - y));
            u8* out = &font.pixels[(ay * kAtlasWidth + ax) * 4];
            for (int c = 0; c < 3; ++c)
            {
                const float value = v[channels == 3 ? c : 0];
                out[c] = static_cast<u8>(std::clamp(std::lround(value * 255.0f), 0L, 255L));
            }
        };
        if (spec.kind == asset::FontKind::Msdf)
        {
            msdfgen::edgeColoringSimple(job.shape, 3.0, 0);
            msdfgen::Bitmap<float, 3> bitmap(job.width, job.height);
            msdfgen::generateMSDF(bitmap, job.shape, transformation);
            for (int y = 0; y < job.height; ++y)
            {
                for (int x = 0; x < job.width; ++x)
                {
                    write(x, y, bitmap(x, y), 3);
                }
            }
        }
        else
        {
            msdfgen::Bitmap<float, 1> bitmap(job.width, job.height);
            msdfgen::generateSDF(bitmap, job.shape, transformation);
            for (int y = 0; y < job.height; ++y)
            {
                for (int x = 0; x < job.width; ++x)
                {
                    write(x, y, bitmap(x, y), 1);
                }
            }
        }
    }

    // Glyph records (codepoint order = spec order, already sorted).
    std::vector<int> atlasX(jobs.size(), 0);
    std::vector<int> atlasY(jobs.size(), 0);
    for (const stbrp_rect& r : rects)
    {
        atlasX[static_cast<usize>(r.id)] = r.x;
        atlasY[static_cast<usize>(r.id)] = r.y;
    }
    for (usize i = 0; i < jobs.size(); ++i)
    {
        const GlyphJob& job = jobs[i];
        asset::FontGlyph g;
        g.codepoint = job.codepoint;
        g.advance = job.advance;
        if (!job.empty)
        {
            g.planeLeft = static_cast<f32>(job.left / em);
            g.planeBottom = static_cast<f32>(job.bottom / em);
            g.planeRight = static_cast<f32>((job.left + job.width) / em);
            g.planeTop = static_cast<f32>((job.bottom + job.height) / em);
            g.atlasX = static_cast<u16>(atlasX[i]);
            g.atlasY = static_cast<u16>(atlasY[i]);
            g.atlasWidth = static_cast<u16>(job.width);
            g.atlasHeight = static_cast<u16>(job.height);
        }
        font.glyphs.push_back(g);
    }

    // Kerning between every pair of included characters (kern table or GPOS pair adjustments).
    for (const GlyphJob& a : jobs)
    {
        for (const GlyphJob& b : jobs)
        {
            const int kern = stbtt_GetGlyphKernAdvance(&info, a.index, b.index);
            if (kern != 0)
            {
                font.kerning.push_back({a.codepoint, b.codepoint, static_cast<f32>(kern * unitsToEm)});
            }
        }
    }
    return font;
#endif
}

Result<void> cookFont(const fs::Path& specPath, const fs::Path& outPath)
{
    const std::string name = fs::toUtf8(specPath);
    auto text = fs::readText(specPath);
    if (!text)
    {
        return text.error();
    }
    auto spec = parseFontSpec(text.value(), name);
    if (!spec)
    {
        return spec.error();
    }
    const fs::Path fontPath = specPath.parent_path() / fs::fromUtf8(spec.value().source);
    auto bytes = fs::readFile(fontPath);
    if (!bytes)
    {
        return bytes.error();
    }
    auto font = buildFont(bytes.value(), spec.value(), fs::toUtf8(fontPath));
    if (!font)
    {
        return font.error();
    }
    const std::vector<u8> out = asset::serializeFont(font.value());
    if (auto dir = outPath.has_parent_path() ? fs::createDirectories(outPath.parent_path()) : Result<void>{};
        !dir)
    {
        return dir.error();
    }
    if (auto written = fs::writeFileAtomic(outPath, out); !written)
    {
        return written.error();
    }
    G7_LOG_INFO("cook", "{}: {} glyphs, {} kerning pairs, atlas {}x{}", fs::toUtf8(outPath),
                font.value().glyphs.size(), font.value().kerning.size(), font.value().atlasWidth,
                font.value().atlasHeight);
    return {};
}
} // namespace g7::cook

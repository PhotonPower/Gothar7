#include <g7/core/Log.hpp>
#include <g7/render/Device.hpp>
#include <g7/ui/Font.hpp>

#include <algorithm>

namespace g7::ui
{
Result<Font> Font::fromData(asset::FontData data, std::string name)
{
    if (data.glyphs.empty())
    {
        return Error{name + ": font without glyphs"};
    }
    if (data.pixels.size() != static_cast<usize>(data.atlasWidth) * data.atlasHeight * 4)
    {
        return Error{name + ": atlas size does not match its pixels"};
    }
    if (!(data.emPixels > 0.0f) || !(data.distanceRange > 0.0f))
    {
        return Error{name + ": invalid font metrics"};
    }
    Font font;
    font.m_data = std::move(data);
    font.m_name = std::move(name);
    return font;
}

Result<Font> Font::load(std::span<const u8> bytes, std::string name)
{
    auto data = asset::deserializeFont(bytes, name);
    if (!data)
    {
        return data.error();
    }
    return fromData(std::move(data).value(), std::move(name));
}

const asset::FontGlyph* Font::find(u32 codepoint) const noexcept
{
    const auto& glyphs = m_data.glyphs;
    const auto it = std::lower_bound(glyphs.begin(), glyphs.end(), codepoint,
                                     [](const asset::FontGlyph& g, u32 c) { return g.codepoint < c; });
    return it != glyphs.end() && it->codepoint == codepoint ? &*it : nullptr;
}

const asset::FontGlyph& Font::glyph(u32 codepoint) const
{
    if (const auto* found = find(codepoint))
    {
        return *found;
    }
    if (m_reportedMissing.insert(codepoint).second)
    {
        G7_LOG_WARN("ui", "{}: no glyph for U+{:04X}, drawn as a replacement", m_name, codepoint);
    }
    for (const u32 fallback : {0xFFFDu, static_cast<u32>('?')})
    {
        if (const auto* found = find(fallback))
        {
            return *found;
        }
    }
    return m_data.glyphs.front();
}

f32 Font::kerning(u32 left, u32 right) const noexcept
{
    const auto& pairs = m_data.kerning;
    const auto it =
        std::lower_bound(pairs.begin(), pairs.end(), std::pair{left, right},
                         [](const asset::FontKerning& k, const std::pair<u32, u32>& key)
                         { return k.left < key.first || (k.left == key.first && k.right < key.second); });
    return it != pairs.end() && it->left == left && it->right == right ? it->advance : 0.0f;
}

f32 Font::screenPixelRange(f32 pixelSize) const noexcept
{
    return std::max(1.0f, m_data.distanceRange * pixelSize / m_data.emPixels);
}

Result<void> Font::upload(render::Device& device)
{
    auto texture =
        device.createTexture({m_data.atlasWidth, m_data.atlasHeight, render::rhi::Format::RGBA8, 1});
    if (!texture)
    {
        return texture.error();
    }
    if (auto uploaded = texture.value().upload(0, m_data.pixels); !uploaded)
    {
        return uploaded.error();
    }
    m_texture = std::move(texture).value();
    m_uploaded = true;
    return {};
}
} // namespace g7::ui

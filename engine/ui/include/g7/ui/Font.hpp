#pragma once

// A distance-field font of the game UI (M14 part A, ADR 0020): the cooked ".g7font" (glyphs, kerning, atlas)
// and, once uploaded, its atlas texture. Text layout: Text.hpp; drawing: Canvas.hpp.

#include <g7/asset/FontFile.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace g7::render
{
class Device;
}

namespace g7::ui
{
class Font
{
public:
    Font() = default;
    /// Checks the data (at least one glyph, an atlas of the stated size).
    [[nodiscard]] static Result<Font> fromData(asset::FontData data, std::string name);
    /// Parses a ".g7font" file.
    [[nodiscard]] static Result<Font> load(std::span<const u8> bytes, std::string name);

    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const asset::FontData& data() const noexcept { return m_data; }

    /// The glyph of `codepoint`, or nullptr if the font lacks it.
    [[nodiscard]] const asset::FontGlyph* find(u32 codepoint) const noexcept;
    /// The glyph of `codepoint`; a missing one falls back to U+FFFD, '?', then the first glyph, and is
    /// reported once in the log.
    [[nodiscard]] const asset::FontGlyph& glyph(u32 codepoint) const;
    /// Extra advance between two codepoints in em (0 without a pair).
    [[nodiscard]] f32 kerning(u32 left, u32 right) const noexcept;

    [[nodiscard]] f32 ascender() const noexcept { return m_data.ascender; }   ///< em
    [[nodiscard]] f32 descender() const noexcept { return m_data.descender; } ///< em, negative
    [[nodiscard]] f32 lineHeight() const noexcept                             ///< em
    {
        return m_data.ascender - m_data.descender + m_data.lineGap;
    }
    /// Screen pixels the distance field spans when drawn at `pixelSize` per em (the shader's anti-aliasing
    /// width); at least 1.
    [[nodiscard]] f32 screenPixelRange(f32 pixelSize) const noexcept;

    /// Uploads the atlas (RGBA8, linear). Without upload, text is laid out but not drawn (tests).
    [[nodiscard]] Result<void> upload(render::Device& device);
    [[nodiscard]] const render::rhi::Texture* texture() const noexcept
    {
        return m_uploaded ? &m_texture : nullptr;
    }

private:
    asset::FontData m_data;
    std::string m_name;
    render::rhi::Texture m_texture;
    bool m_uploaded = false;
    mutable std::unordered_set<u32> m_reportedMissing;
};
} // namespace g7::ui

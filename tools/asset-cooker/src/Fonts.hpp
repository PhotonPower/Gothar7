#pragma once

// Font cooking (M14, ADR 0020): a TrueType/OpenType font and a description `<name>.font.toml` become a
// distance-field atlas with metrics, ".g7font" (asset/FontFile.hpp). Outlines and kerning come from
// stb_truetype, the multi-channel distance field from msdfgen (core only), packing from stb_rect_pack.

#include <g7/asset/FontFile.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::cook
{
/// `<name>.font.toml`, version 1:
///
///     version = 1
///     source = "Alegreya-VF.ttf"            # font file, relative to the description
///     kind = "msdf"                          # or "sdf"
///     size = 48                              # pixels per em in the atlas
///     range = 6                              # distance range in atlas pixels
///     charset = ["ascii", "latin1", "typography"]
///     extra = "€"                            # further characters (UTF-8)
struct FontSpec
{
    std::string source;
    asset::FontKind kind = asset::FontKind::Msdf;
    f32 emPixels = 48.0f;
    f32 distanceRange = 6.0f;
    std::vector<u32> codepoints; ///< sorted, unique
};

/// Codepoints of a named set: "ascii" (U+0020-U+007E), "latin1" (U+00A0-U+00FF), "typography"
/// (dashes, typographic quotes, guillemets, bullet, ellipsis, euro sign). Empty for unknown names.
[[nodiscard]] std::vector<u32> charsetCodepoints(std::string_view name);

[[nodiscard]] Result<FontSpec> parseFontSpec(std::string_view toml,
                                             std::string_view debugName = "<font.toml>");

/// Builds the atlas. Characters the font lacks are left out with a warning; fails if the font cannot be read
/// or has none of the characters. Deterministic: same input, same bytes.
[[nodiscard]] Result<asset::FontData> buildFont(std::span<const u8> fontFile, const FontSpec& spec,
                                                std::string_view debugName = "<font>");

/// Reads `specPath` and its font, builds the atlas and writes `outPath` (.g7font).
[[nodiscard]] Result<void> cookFont(const fs::Path& specPath, const fs::Path& outPath);

/// Whether this build can generate fonts (msdfgen available).
[[nodiscard]] bool fontCookingAvailable() noexcept;
} // namespace g7::cook

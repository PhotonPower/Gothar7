#pragma once

// Look of the game UI as data (M14 part A): assets/source/ui/style.toml - colours, text roles (font, size,
// outline), 9-slice frames and UI sounds. Screens ask for names ("body", "panel"); the values stay editable.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <map>
#include <string>
#include <string_view>

namespace g7::ui
{
/// A colour as written to the screen (display values, not linear), 0..1.
struct Color
{
    f32 r = 1.0f;
    f32 g = 1.0f;
    f32 b = 1.0f;
    f32 a = 1.0f;

    [[nodiscard]] Color withAlpha(f32 alpha) const noexcept { return {r, g, b, alpha}; }
    /// RGBA8 packed little-endian (r in the lowest byte), as the vertex format expects.
    [[nodiscard]] u32 packed() const noexcept;
    friend bool operator==(const Color&, const Color&) = default;
};

/// "#rrggbb" or "#rrggbbaa" (hex, either case).
[[nodiscard]] Result<Color> parseColor(std::string_view text);

/// How a kind of text looks.
struct TextRole
{
    std::string font; ///< VFS path of the .g7font
    f32 size = 22.0f; ///< UI units (pixels at 1080p)
    Color color;
    f32 outline = 0.0f; ///< outline width in UI units (0 = none); at most half the font's distance range
    Color outlineColor{0.0f, 0.0f, 0.0f, 1.0f};
    f32 shadow = 0.0f; ///< drop shadow offset in UI units (0 = none)
    Color shadowColor{0.0f, 0.0f, 0.0f, 0.6f};
};

/// A frame drawn from an image in nine parts: corners keep their size, edges stretch along one axis.
struct FrameStyle
{
    std::string image; ///< VFS path
    f32 left = 0.0f;   ///< border widths in image pixels
    f32 top = 0.0f;
    f32 right = 0.0f;
    f32 bottom = 0.0f;
    f32 scale = 1.0f; ///< UI units per image pixel for the borders
    Color tint;
};

/// The studio light of the cells (style.toml [item_preview]; like figuren's review sheet: sun from the upper
/// left front, bright ambient).
struct PreviewLight
{
    Vec3 sunDirection{-0.5f, 0.6f, 0.6f}; ///< towards the sun, view space (+X right, +Y up, +Z to the viewer)
    Vec3 sunColor{1.0f, 0.96f, 0.9f};
    f32 sunIntensity = 3.0f;
    Vec3 ambientSky{0.55f, 0.55f, 0.6f};
    Vec3 ambientGround{0.3f, 0.28f, 0.25f};
    f32 exposure = 1.0f;
};

/// style.toml, version 1:
///
///     version = 1
///     [colors]
///     text = "#e8dcc0"
///     [fonts]                                   # text roles
///     body = { font = "fonts/alegreya.g7font", size = 22, color = "text" }   # a colour name or "#..."
///     title = { font = "fonts/alegreya_sc.g7font", size = 34, color = "highlight", outline = 1.5 }
///     [frames.panel]
///     image = "ui/frames/panel.png"
///     slice = [24, 24, 24, 24]                  # left, top, right, bottom in image pixels
///     scale = 1.0
///     [sounds]
///     select = "ui_select"                      # sound names (data/sounds.toml)
struct Style
{
    std::map<std::string, Color, std::less<>> colors;
    std::map<std::string, TextRole, std::less<>> fonts;
    std::map<std::string, FrameStyle, std::less<>> frames;
    std::map<std::string, std::string, std::less<>> sounds;
    PreviewLight itemPreview;   ///< [item_preview] (M14 part D)
    f32 itemSpinSeconds = 6.0f; ///< [item_preview] spin_seconds: one turn of the selected item

    /// A named colour; unknown names log a warning (once) and give magenta.
    [[nodiscard]] Color color(std::string_view name) const;
    /// A text role; unknown names log a warning (once) and give a default role.
    [[nodiscard]] const TextRole& font(std::string_view role) const;
    [[nodiscard]] const FrameStyle* frame(std::string_view name) const;
    /// The sound for a UI event, empty if none.
    [[nodiscard]] std::string_view sound(std::string_view event) const;
};

/// Parses style.toml. Errors name the file and the entry; unknown top-level tables are warnings.
[[nodiscard]] Result<Style> loadStyle(std::string_view toml, std::string_view sourceName = "ui/style.toml");
} // namespace g7::ui

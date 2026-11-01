#pragma once

// What the game UI loads (M14 part A): the style (ui/style.toml), the texts of one language
// (text/ui.<language>.toml), every font the style's text roles name and every frame image. Missing fonts
// or images are warnings (text or frame not drawn); a broken style or text table is an error.

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/rhi/Resources.hpp>
#include <g7/ui/Canvas.hpp>
#include <g7/ui/Font.hpp>
#include <g7/ui/Strings.hpp>
#include <g7/ui/Style.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::render
{
class Device;
}

namespace g7::ui
{
inline constexpr std::string_view kStylePath = "ui/style.toml";

/// Reads a file from the virtual file system (the engine passes its VFS).
using ReadFile = std::function<Result<std::vector<u8>>(std::string_view path)>;

/// "text/ui.<language>.toml".
[[nodiscard]] std::string stringsPath(std::string_view language);

class UiResources
{
public:
    /// Loads everything; with a device, fonts and frame images are uploaded.
    [[nodiscard]] static Result<UiResources> load(const ReadFile& read, std::string language,
                                                  render::Device* device);

    UiResources();
    ~UiResources();
    UiResources(UiResources&&) noexcept;
    UiResources& operator=(UiResources&&) noexcept;
    UiResources(const UiResources&) = delete;
    UiResources& operator=(const UiResources&) = delete;

    [[nodiscard]] const Style& style() const noexcept { return m_style; }
    [[nodiscard]] const Strings& strings() const noexcept { return m_strings; }
    /// A loaded font by VFS path, nullptr if it failed to load.
    [[nodiscard]] const Font* font(std::string_view path) const;

    /// A text role at `uiScale` (framebuffer pixels per UI unit): font, colours, outline in pixels.
    [[nodiscard]] TextStyle textStyle(std::string_view role, f32 uiScale) const;
    /// The role's font size in framebuffer pixels.
    [[nodiscard]] f32 textSize(std::string_view role, f32 uiScale) const;
    /// Lays out `utf8` in a role (size from the role; wrap width etc. from `options`).
    [[nodiscard]] TextLayout layout(std::string_view role, std::string_view utf8, f32 uiScale,
                                    TextOptions options = {}) const;
    /// A frame at `uiScale`; without its image the slice has no texture (Canvas draws a plain rectangle).
    [[nodiscard]] NineSlice frame(std::string_view name, f32 uiScale) const;
    /// The tint of a frame (white for unknown frames).
    [[nodiscard]] Color frameTint(std::string_view name) const;

private:
    struct Image
    {
        render::rhi::Texture texture;
        Vec2 size{0.0f};
        bool uploaded = false;
    };
    Style m_style;
    Strings m_strings;
    std::map<std::string, std::unique_ptr<Font>, std::less<>> m_fonts;
    std::map<std::string, std::unique_ptr<Image>, std::less<>> m_images;
};
} // namespace g7::ui

#include <g7/asset/ImageData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Log.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/TextureUpload.hpp>
#include <g7/ui/UiResources.hpp>

#include <string>

namespace g7::ui
{
namespace
{
std::string textOf(const std::vector<u8>& bytes)
{
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

/// A UI image: the file as named, or its cooked KTX2 sibling (g7-cook turns images into .ktx2).
Result<asset::TextureData> readImage(const ReadFile& read, const std::string& path)
{
    if (auto bytes = read(path); bytes)
    {
        auto image = asset::decodeImage(bytes.value(), path);
        if (!image)
        {
            return image.error();
        }
        return asset::textureFromImage(std::move(image).value(), true);
    }
    const usize dot = path.rfind('.');
    const std::string ktx2 = (dot == std::string::npos ? path : path.substr(0, dot)) + ".ktx2";
    auto bytes = read(ktx2);
    if (!bytes)
    {
        return Error{path + ": not found (nor " + ktx2 + ")"};
    }
    return asset::decodeKtx2(bytes.value(), ktx2);
}
} // namespace

std::string stringsPath(std::string_view language)
{
    return "text/ui." + std::string(language) + ".toml";
}

UiResources::UiResources() = default;
UiResources::~UiResources() = default;
UiResources::UiResources(UiResources&&) noexcept = default;
UiResources& UiResources::operator=(UiResources&&) noexcept = default;

Result<UiResources> UiResources::load(const ReadFile& read, std::string language, render::Device* device)
{
    UiResources ui;
    auto styleBytes = read(kStylePath);
    if (!styleBytes)
    {
        return Error{std::string(kStylePath) + ": " + styleBytes.error().message};
    }
    auto style = loadStyle(textOf(styleBytes.value()), kStylePath);
    if (!style)
    {
        return style.error();
    }
    ui.m_style = std::move(style).value();
    ui.m_files.emplace_back(kStylePath);

    auto layoutBytes = read(kLayoutPath);
    if (!layoutBytes)
    {
        return Error{std::string(kLayoutPath) + ": " + layoutBytes.error().message};
    }
    auto layout = Layout::load(textOf(layoutBytes.value()), kLayoutPath);
    if (!layout)
    {
        return layout.error();
    }
    ui.m_layout = std::move(layout).value();
    ui.m_files.emplace_back(kLayoutPath);

    const std::string textPath = stringsPath(language);
    auto textBytes = read(textPath);
    if (!textBytes)
    {
        return Error{textPath + ": " + textBytes.error().message};
    }
    auto strings = Strings::load(std::move(language), textOf(textBytes.value()), textPath);
    if (!strings)
    {
        return strings.error();
    }
    ui.m_strings = std::move(strings).value();
    ui.m_files.push_back(textPath);

    for (const auto& [role, text] : ui.m_style.fonts)
    {
        if (ui.m_fonts.contains(text.font))
        {
            continue;
        }
        std::unique_ptr<Font> loaded;
        if (auto bytes = read(text.font); !bytes)
        {
            G7_LOG_WARN("ui", "font {} (text role '{}'): {} - its texts are not drawn (build g7_fonts)",
                        text.font, role, bytes.error().message);
        }
        else if (auto font = Font::load(bytes.value(), text.font); !font)
        {
            G7_LOG_WARN("ui", "{}", font.error().message);
        }
        else
        {
            loaded = std::make_unique<Font>(std::move(font).value());
            if (device != nullptr)
            {
                if (auto uploaded = loaded->upload(*device); !uploaded)
                {
                    G7_LOG_WARN("ui", "font {}: {}", text.font, uploaded.error().message);
                }
            }
        }
        ui.m_fonts.emplace(text.font, std::move(loaded));
        ui.m_files.push_back(text.font);
    }

    for (const auto& [name, frame] : ui.m_style.frames)
    {
        if (ui.m_images.contains(frame.image))
        {
            continue;
        }
        auto image = std::make_unique<Image>();
        if (auto data = readImage(read, frame.image); !data)
        {
            G7_LOG_WARN("ui", "frame '{}': {} - drawn as a plain rectangle", name, data.error().message);
        }
        else
        {
            image->size =
                Vec2(static_cast<f32>(data.value().width()), static_cast<f32>(data.value().height()));
            if (device != nullptr)
            {
                if (auto texture = render::createTexture(*device, data.value(), true); texture)
                {
                    image->texture = std::move(texture).value();
                    image->uploaded = true;
                }
                else
                {
                    G7_LOG_WARN("ui", "frame '{}': {}", name, texture.error().message);
                }
            }
        }
        ui.m_images.emplace(frame.image, std::move(image));
        ui.m_files.push_back(frame.image);
    }
    G7_LOG_INFO("ui", "game UI: {} text roles, {} fonts, {} frames, {} texts ({})", ui.m_style.fonts.size(),
                ui.m_fonts.size(), ui.m_style.frames.size(), ui.m_strings.keys().size(),
                ui.m_strings.language());
    return ui;
}

const Font* UiResources::font(std::string_view path) const
{
    const auto it = m_fonts.find(path);
    return it != m_fonts.end() ? it->second.get() : nullptr;
}

TextStyle UiResources::textStyle(std::string_view role, f32 uiScale) const
{
    const TextRole& r = m_style.font(role);
    return {font(r.font), r.color, r.outline * uiScale, r.outlineColor, r.shadow * uiScale, r.shadowColor};
}

f32 UiResources::textSize(std::string_view role, f32 uiScale) const
{
    return m_style.font(role).size * uiScale;
}

TextLayout UiResources::layout(std::string_view role, std::string_view utf8, f32 uiScale,
                               TextOptions options) const
{
    const Font* f = font(m_style.font(role).font);
    if (f == nullptr)
    {
        return {};
    }
    options.size = textSize(role, uiScale);
    return layoutText(*f, utf8, options);
}

NineSlice UiResources::frame(std::string_view name, f32 uiScale) const
{
    const FrameStyle* style = m_style.frame(name);
    if (style == nullptr)
    {
        return {};
    }
    NineSlice slice;
    const auto it = m_images.find(style->image);
    if (it != m_images.end() && it->second->size.x > 0.0f)
    {
        slice.texture = it->second->uploaded ? &it->second->texture : nullptr;
        slice.imageSize = it->second->size;
    }
    slice.left = style->left;
    slice.top = style->top;
    slice.right = style->right;
    slice.bottom = style->bottom;
    slice.borderScale = style->scale * uiScale;
    return slice;
}

Color UiResources::frameTint(std::string_view name) const
{
    const FrameStyle* style = m_style.frame(name);
    return style != nullptr ? style->tint : Color{};
}
} // namespace g7::ui

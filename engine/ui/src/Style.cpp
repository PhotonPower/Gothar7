#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>
#include <g7/ui/Style.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <set>

namespace g7::ui
{
namespace
{
constexpr std::array<std::string_view, 6> kSections = {"version", "colors", "fonts",
                                                       "frames",  "sounds", "item_preview"};
constexpr Color kMissing{1.0f, 0.0f, 1.0f, 1.0f};

int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

/// Warns once per (kind, name) for the whole program: styles are reloaded, the log should not repeat.
void warnOnce(std::string_view kind, std::string_view name)
{
    static std::mutex mutex;
    static std::set<std::string, std::less<>> reported;
    const std::scoped_lock lock(mutex);
    std::string key = std::string(kind) + ":" + std::string(name);
    if (reported.insert(std::move(key)).second)
    {
        G7_LOG_WARN("ui", "style: unknown {} '{}'", kind, name);
    }
}

Error entryError(std::string_view source, std::string_view entry, std::string_view what)
{
    return Error{std::string(source) + ": " + std::string(entry) + ": " + std::string(what)};
}

/// A colour value: a name from [colors] or "#...".
Result<Color> colorValue(const std::map<std::string, Color, std::less<>>& colors, const std::string& value)
{
    if (value.starts_with('#'))
    {
        return parseColor(value);
    }
    const auto it = colors.find(value);
    if (it == colors.end())
    {
        return Error{"unknown colour '" + value + "'"};
    }
    return it->second;
}
} // namespace

u32 Color::packed() const noexcept
{
    const auto byte = [](f32 v) { return static_cast<u32>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return byte(r) | (byte(g) << 8) | (byte(b) << 16) | (byte(a) << 24);
}

Result<Color> parseColor(std::string_view text)
{
    if (!text.starts_with('#') || (text.size() != 7 && text.size() != 9))
    {
        return Error{"colour '" + std::string(text) + "' is not #rrggbb or #rrggbbaa"};
    }
    std::array<f32, 4> channels{1.0f, 1.0f, 1.0f, 1.0f};
    for (usize i = 0; i + 1 < text.size(); i += 2)
    {
        const int hi = hexDigit(text[i + 1]);
        const int lo = i + 2 < text.size() ? hexDigit(text[i + 2]) : -1;
        if (hi < 0 || lo < 0)
        {
            return Error{"colour '" + std::string(text) + "' has a non-hex digit"};
        }
        channels[i / 2] = static_cast<f32>(hi * 16 + lo) / 255.0f;
    }
    return Color{channels[0], channels[1], channels[2], channels[3]};
}

Color Style::color(std::string_view name) const
{
    const auto it = colors.find(name);
    if (it != colors.end())
    {
        return it->second;
    }
    warnOnce("colour", name);
    return kMissing;
}

const TextRole& Style::font(std::string_view role) const
{
    const auto it = fonts.find(role);
    if (it != fonts.end())
    {
        return it->second;
    }
    warnOnce("text role", role);
    static const TextRole fallback{.font = "fonts/alegreya.g7font", .color = kMissing};
    return fallback;
}

const FrameStyle* Style::frame(std::string_view name) const
{
    const auto it = frames.find(name);
    if (it != frames.end())
    {
        return &it->second;
    }
    warnOnce("frame", name);
    return nullptr;
}

std::string_view Style::sound(std::string_view event) const
{
    const auto it = sounds.find(event);
    return it != sounds.end() ? std::string_view(it->second) : std::string_view{};
}

Result<Style> loadStyle(std::string_view toml, std::string_view sourceName)
{
    auto parsed = Config::parse(toml, sourceName);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& config = parsed.value();
    if (config.get<i64>("version", 0) != 1)
    {
        return Error{std::string(sourceName) + ": version must be 1"};
    }
    for (const std::string& key : config.keys())
    {
        if (std::find(kSections.begin(), kSections.end(), key) == kSections.end())
        {
            G7_LOG_WARN("ui", "{}: unknown section '{}' ignored", sourceName, key);
        }
    }
    Style style;
    for (const std::string& name : config.keys("colors"))
    {
        auto color = parseColor(config.get<std::string>("colors." + name, ""));
        if (!color)
        {
            return entryError(sourceName, "colors." + name, color.error().message);
        }
        style.colors.emplace(name, color.value());
    }
    for (const std::string& name : config.keys("fonts"))
    {
        const std::string base = "fonts." + name + ".";
        TextRole role;
        role.font = config.get<std::string>(base + "font", "");
        if (role.font.empty())
        {
            return entryError(sourceName, "fonts." + name, "'font' is missing");
        }
        role.size = static_cast<f32>(config.get<f64>(base + "size", 22.0));
        role.outline = static_cast<f32>(config.get<f64>(base + "outline", 0.0));
        if (!(role.size > 0.0f) || role.outline < 0.0f)
        {
            return entryError(sourceName, "fonts." + name, "size must be > 0, outline >= 0");
        }
        if (auto color = colorValue(style.colors, config.get<std::string>(base + "color", "#ffffff")); color)
        {
            role.color = color.value();
        }
        else
        {
            return entryError(sourceName, "fonts." + name, color.error().message);
        }
        if (auto color = colorValue(style.colors, config.get<std::string>(base + "outline_color", "#000000"));
            color)
        {
            role.outlineColor = color.value();
        }
        else
        {
            return entryError(sourceName, "fonts." + name, color.error().message);
        }
        role.shadow = static_cast<f32>(config.get<f64>(base + "shadow", 0.0));
        if (auto color =
                colorValue(style.colors, config.get<std::string>(base + "shadow_color", "#00000099"));
            color)
        {
            role.shadowColor = color.value();
        }
        else
        {
            return entryError(sourceName, "fonts." + name, color.error().message);
        }
        style.fonts.emplace(name, std::move(role));
    }
    for (const std::string& name : config.keys("frames"))
    {
        const std::string base = "frames." + name + ".";
        FrameStyle frame;
        frame.image = config.get<std::string>(base + "image", "");
        const auto slice = config.get<std::vector<f64>>(base + "slice", {});
        if (frame.image.empty() || slice.size() != 4)
        {
            return entryError(sourceName, "frames." + name,
                              "needs 'image' and 'slice' = [left, top, right, bottom]");
        }
        frame.left = static_cast<f32>(slice[0]);
        frame.top = static_cast<f32>(slice[1]);
        frame.right = static_cast<f32>(slice[2]);
        frame.bottom = static_cast<f32>(slice[3]);
        frame.scale = static_cast<f32>(config.get<f64>(base + "scale", 1.0));
        if (frame.left < 0 || frame.top < 0 || frame.right < 0 || frame.bottom < 0 || !(frame.scale > 0.0f))
        {
            return entryError(sourceName, "frames." + name, "slice values must be >= 0 and scale > 0");
        }
        if (auto tint = colorValue(style.colors, config.get<std::string>(base + "tint", "#ffffff")); tint)
        {
            frame.tint = tint.value();
        }
        else
        {
            return entryError(sourceName, "frames." + name, tint.error().message);
        }
        style.frames.emplace(name, std::move(frame));
    }
    const auto vec3 = [&](std::string_view key, Vec3 fallback) -> Result<Vec3>
    {
        const auto values = config.find<std::vector<f64>>("item_preview." + std::string(key));
        if (!values)
        {
            return fallback;
        }
        if (values->size() != 3)
        {
            return entryError(sourceName, "item_preview." + std::string(key), "needs three numbers");
        }
        return Vec3(static_cast<f32>((*values)[0]), static_cast<f32>((*values)[1]),
                    static_cast<f32>((*values)[2]));
    };
    PreviewLight& light = style.itemPreview;
    for (auto [key, target] : {std::pair<std::string_view, Vec3*>{"sun_direction", &light.sunDirection},
                               {"sun_color", &light.sunColor},
                               {"ambient_sky", &light.ambientSky},
                               {"ambient_ground", &light.ambientGround}})
    {
        auto value = vec3(key, *target);
        if (!value)
        {
            return value.error();
        }
        *target = value.value();
    }
    light.sunIntensity = static_cast<f32>(config.get<f64>("item_preview.sun_intensity", light.sunIntensity));
    light.exposure = static_cast<f32>(config.get<f64>("item_preview.exposure", light.exposure));
    style.itemSpinSeconds =
        static_cast<f32>(config.get<f64>("item_preview.spin_seconds", style.itemSpinSeconds));
    for (const std::string& name : config.keys("sounds"))
    {
        style.sounds.emplace(name, config.get<std::string>("sounds." + name, ""));
    }
    return style;
}
} // namespace g7::ui

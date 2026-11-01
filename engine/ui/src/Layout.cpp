#include <g7/core/Log.hpp>
#include <g7/ui/Layout.hpp>

#include <algorithm>
#include <array>

namespace g7::ui
{
namespace
{
constexpr std::array<std::string_view, 9> kAnchorNames = {
    "top_left", "top", "top_right", "left", "center", "right", "bottom_left", "bottom", "bottom_right"};
} // namespace

std::optional<Anchor> anchorFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < kAnchorNames.size(); ++i)
    {
        if (kAnchorNames[i] == name)
        {
            return static_cast<Anchor>(i);
        }
    }
    return std::nullopt;
}

Rect place(const LayoutBox& box, const Rect& parent, f32 scale) noexcept
{
    const Vec2 parentSize = parent.size();
    Vec2 size = box.size * scale;
    for (int axis = 0; axis < 2; ++axis)
    {
        if (box.size[axis] <= 0.0f)
        {
            size[axis] = std::max(0.0f, parentSize[axis] + box.size[axis] * scale);
        }
    }
    const auto index = static_cast<int>(box.anchor);
    const f32 fx = static_cast<f32>(index % 3) * 0.5f; // 0 left, 0.5 centre, 1 right
    const f32 fy = static_cast<f32>(index / 3) * 0.5f; // 0 top, 0.5 middle, 1 bottom
    const Vec2 factor(fx, fy);
    const Vec2 min = parent.min + (parentSize - size) * factor + box.offset * scale;
    return {min, min + size};
}

Result<Layout> Layout::load(std::string_view toml, std::string_view sourceName)
{
    auto parsed = Config::parse(toml, sourceName);
    if (!parsed)
    {
        return parsed.error();
    }
    if (parsed.value().get<i64>("version", 0) != 1)
    {
        return Error{std::string(sourceName) + ": version must be 1"};
    }
    Layout layout;
    layout.m_config = std::move(parsed).value();
    return layout;
}

bool Layout::contains(std::string_view key) const
{
    return m_config.contains(key);
}

void Layout::reportMissing(std::string_view key) const
{
    if (m_reported.emplace(key).second)
    {
        G7_LOG_WARN("ui", "layout: '{}' missing or invalid, using the default", key);
    }
}

LayoutBox Layout::box(std::string_view key, const LayoutBox& fallback) const
{
    const std::string base(key);
    if (!m_config.contains(base))
    {
        reportMissing(key);
        return fallback;
    }
    LayoutBox box = fallback;
    if (const auto anchor = m_config.find<std::string>(base + ".anchor"))
    {
        if (const auto parsed = anchorFromName(*anchor))
        {
            box.anchor = *parsed;
        }
        else
        {
            reportMissing(base + ".anchor");
        }
    }
    const auto vec2 = [&](const char* field, Vec2& out)
    {
        if (const auto values = m_config.find<std::vector<f64>>(base + "." + field))
        {
            if (values->size() == 2)
            {
                out = Vec2(static_cast<f32>((*values)[0]), static_cast<f32>((*values)[1]));
            }
            else
            {
                reportMissing(base + "." + field);
            }
        }
    };
    vec2("offset", box.offset);
    vec2("size", box.size);
    return box;
}

f32 Layout::number(std::string_view key, f32 fallback) const
{
    if (const auto value = m_config.find<f64>(key))
    {
        return static_cast<f32>(*value);
    }
    reportMissing(key);
    return fallback;
}
} // namespace g7::ui

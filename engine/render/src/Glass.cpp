#include <g7/render/Glass.hpp>

#include <algorithm>

namespace g7::render
{
std::optional<WindowReflections> parseWindowReflections(std::string_view text) noexcept
{
    for (const WindowReflections quality :
         {WindowReflections::Off, WindowReflections::Low, WindowReflections::Medium, WindowReflections::High})
    {
        if (text == windowReflectionsName(quality))
        {
            return quality;
        }
    }
    return std::nullopt;
}

std::string_view windowReflectionsName(WindowReflections quality) noexcept
{
    switch (quality)
    {
    case WindowReflections::Off:
        return "off";
    case WindowReflections::Low:
        return "low";
    case WindowReflections::Medium:
        return "medium";
    case WindowReflections::High:
        break;
    }
    return "high";
}

f32 glassFresnel(f32 cosTheta) noexcept
{
    const f32 m = 1.0f - std::clamp(cosTheta, 0.0f, 1.0f);
    const f32 m2 = m * m;
    return std::min(kGlassF0 + (1.0f - kGlassF0) * m2 * m2 * m, kGlassMaxFresnel);
}
} // namespace g7::render

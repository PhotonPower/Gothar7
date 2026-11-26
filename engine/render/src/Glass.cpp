#include <g7/render/Glass.hpp>

#include <algorithm>
#include <array>
#include <cmath>

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

f32 windowsAwake(f32 hours) noexcept
{
    // (hour, share): game feeling, render.md "Spiegelnde Fenster" (F1b).
    static constexpr std::array<std::array<f32, 2>, 10> kKeys{{{0.0f, 0.08f},
                                                               {4.5f, 0.04f},
                                                               {5.5f, 0.25f},
                                                               {7.0f, 0.3f},
                                                               {8.0f, 0.1f},
                                                               {17.0f, 0.3f},
                                                               {19.0f, 0.55f},
                                                               {21.0f, 0.5f},
                                                               {22.5f, 0.2f},
                                                               {24.0f, 0.08f}}};
    f32 h = std::fmod(hours, 24.0f);
    if (!(h >= 0.0f))
    {
        h = std::isfinite(h) ? h + 24.0f : 0.0f;
    }
    for (usize i = 1; i < kKeys.size(); ++i)
    {
        if (h <= kKeys[i][0])
        {
            const f32 t = (h - kKeys[i - 1][0]) / (kKeys[i][0] - kKeys[i - 1][0]);
            return kKeys[i - 1][1] + (kKeys[i][1] - kKeys[i - 1][1]) * t;
        }
    }
    return kKeys.back()[1];
}

f32 windowGlowNight(f32 sunHeight) noexcept
{
    const f32 t = std::clamp((0.12f - sunHeight) / 0.2f, 0.0f, 1.0f); // 1 below -0.08, 0 above 0.12
    return t * t * (3.0f - 2.0f * t);
}
} // namespace g7::render

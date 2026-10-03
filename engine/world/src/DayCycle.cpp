#include <g7/asset/Vfs.hpp>
#include <g7/core/Config.hpp>
#include <g7/world/DayCycle.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace g7::world
{
namespace
{
f32 srgbToLinear(f32 c) noexcept
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Vec3 srgbToLinear(const Vec3& c) noexcept
{
    return Vec3(srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b));
}

f32 radians(f32 degrees) noexcept
{
    return degrees * std::numbers::pi_v<f32> / 180.0f;
}

/// Angle on the circle of the day: 0 at sunrise, pi at sunset, 2 pi at the next sunrise (day and
/// night halves each spread over their own length).
f32 orbitAngle(const Orbit& orbit, f32 hour) noexcept
{
    const f32 dayLength = orbit.sunset - orbit.sunrise;
    const f32 sinceSunrise = std::fmod(hour - orbit.sunrise + 48.0f, 24.0f);
    const f32 pi = std::numbers::pi_v<f32>;
    return sinceSunrise < dayLength ? sinceSunrise / dayLength * pi
                                    : pi + (sinceSunrise - dayLength) / (24.0f - dayLength) * pi;
}

template <typename T>
T blend(const T& a, const T& b, f32 t)
{
    return a + (b - a) * t;
}
} // namespace

Vec3 Orbit::sunDirection(f32 hour) const noexcept
{
    const f32 angle = orbitAngle(*this, hour);
    const f32 elevation = radians(noonElevation);
    return glm::normalize(
        Vec3(std::cos(angle), std::sin(angle) * std::sin(elevation), std::sin(angle) * std::cos(elevation)));
}

Vec3 Orbit::moonDirection(f32 hour) const noexcept
{
    const f32 angle = orbitAngle(*this, hour);
    const f32 elevation = radians(moonElevation);
    return glm::normalize(Vec3(-std::cos(angle), -std::sin(angle) * std::sin(elevation),
                               -std::sin(angle) * std::cos(elevation)));
}

Result<DayCycle> DayCycle::parse(std::string_view toml, std::string_view source)
{
    auto config = Config::parse(toml, source);
    if (!config)
    {
        return config.error();
    }
    const Config& c = config.value();
    const auto error = [&](std::string_view where, std::string_view what)
    { return Error{std::format("{}: {}: {}", source, where, what)}; };
    if (c.get<i64>("version", 0) != 1)
    {
        return error("version", "must be 1");
    }
    DayCycle cycle;
    Orbit& o = cycle.m_orbit;
    o.sunrise = static_cast<f32>(c.get<f64>("orbit.sunrise", o.sunrise));
    o.sunset = static_cast<f32>(c.get<f64>("orbit.sunset", o.sunset));
    o.noonElevation = static_cast<f32>(c.get<f64>("orbit.noon_elevation", o.noonElevation));
    o.moonElevation = static_cast<f32>(c.get<f64>("orbit.moon_elevation", o.moonElevation));
    if (!(o.sunrise >= 0.0f && o.sunrise < o.sunset && o.sunset <= 24.0f))
    {
        return error("orbit", "needs 0 <= sunrise < sunset <= 24");
    }
    const usize count = c.arraySize("key");
    if (count == 0)
    {
        return error("key", "needs at least one [[key]]");
    }
    for (usize i = 0; i < count; ++i)
    {
        const std::string at = std::format("key[{}]", i);
        EnvironmentKey key;
        const auto hour = c.find<f64>(at + ".hour");
        if (!hour || *hour < 0.0 || *hour >= 24.0)
        {
            return error(at + ".hour", "needs an hour 0 <= hour < 24");
        }
        key.hour = static_cast<f32>(*hour);
        // Colours: sRGB 0..1 in the file (what one picks in a colour chooser), linear here.
        for (auto [name, target] :
             {std::pair{"sun_color", &key.sunColor}, std::pair{"moon_color", &key.moonColor},
              std::pair{"ambient_sky", &key.ambientSky}, std::pair{"ambient_ground", &key.ambientGround},
              std::pair{"fog_color", &key.fogColor}, std::pair{"sky_zenith", &key.skyZenith}})
        {
            const std::string where = at + "." + name;
            if (const auto value = c.find<std::vector<f64>>(where))
            {
                if (value->size() != 3 ||
                    std::any_of(value->begin(), value->end(), [](f64 v) { return v < 0.0; }))
                {
                    return error(where, "must be [r, g, b] (sRGB, 0..1)");
                }
                *target = srgbToLinear(Vec3((*value)[0], (*value)[1], (*value)[2]));
            }
            else if (c.contains(where))
            {
                return error(where, "must be [r, g, b] (sRGB, 0..1)");
            }
            else
            {
                *target = srgbToLinear(*target); // defaults are given in sRGB too
            }
        }
        for (auto [name, target] :
             {std::pair{"sun_intensity", &key.sunIntensity}, std::pair{"moon_intensity", &key.moonIntensity},
              std::pair{"fog_density", &key.fogDensity}, std::pair{"stars", &key.stars}})
        {
            const f64 value = c.get<f64>(at + "." + name, *target);
            if (value < 0.0)
            {
                return error(at + "." + name, "must not be negative");
            }
            *target = static_cast<f32>(value);
        }
        cycle.m_keys.push_back(key);
    }
    std::sort(cycle.m_keys.begin(), cycle.m_keys.end(),
              [](const EnvironmentKey& a, const EnvironmentKey& b) { return a.hour < b.hour; });
    for (usize i = 1; i < cycle.m_keys.size(); ++i)
    {
        if (cycle.m_keys[i].hour == cycle.m_keys[i - 1].hour)
        {
            return error("key", std::format("two keys at hour {}", cycle.m_keys[i].hour));
        }
    }
    return cycle;
}

Result<DayCycle> DayCycle::load(const asset::Vfs& vfs, std::string_view path)
{
    auto bytes = vfs.read(path);
    if (!bytes)
    {
        return bytes.error();
    }
    return parse(std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()),
                 path);
}

DayCycle DayCycle::fallback()
{
    // The dusk of M2/M3: low warm sun, cool ambient, dark red-brown horizon.
    DayCycle cycle;
    EnvironmentKey key;
    key.sunColor = Vec3(1.0f, 0.72f, 0.5f);
    key.sunIntensity = 3.0f;
    key.ambientSky = Vec3(0.16f, 0.18f, 0.26f);
    key.ambientGround = Vec3(0.07f, 0.06f, 0.05f);
    key.fogColor = Vec3(0.0844f, 0.0395f, 0.0331f);
    key.skyZenith = Vec3(0.0052f, 0.0080f, 0.0170f);
    cycle.m_keys.push_back(key);
    cycle.m_orbit.noonElevation = 15.0f;
    return cycle;
}

DaySample DayCycle::evaluate(f32 hour, f32 fogDensity) const
{
    hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
    // The keys around `hour`, cyclic: after the last comes the first of the next day.
    usize next = 0;
    while (next < m_keys.size() && m_keys[next].hour <= hour)
    {
        ++next;
    }
    const EnvironmentKey& b = m_keys[next % m_keys.size()];
    const EnvironmentKey& a = m_keys[(next + m_keys.size() - 1) % m_keys.size()];
    const f32 span = std::fmod(b.hour - a.hour + 24.0f, 24.0f);
    const f32 t =
        span > 0.0f ? glm::smoothstep(0.0f, 1.0f, std::fmod(hour - a.hour + 24.0f, 24.0f) / span) : 0.0f;

    DaySample sample;
    render::Environment& env = sample.environment;
    const Vec3 sun = m_orbit.sunDirection(hour);
    const Vec3 moon = m_orbit.moonDirection(hour);
    // One directional light: the sun while it is up, then the moon; both fade at the horizon so the
    // change at dusk and dawn is not a jump.
    const f32 sunUp = glm::smoothstep(-0.02f, 0.08f, sun.y);
    const f32 moonUp = glm::smoothstep(-0.02f, 0.08f, moon.y);
    const f32 sunPower = blend(a.sunIntensity, b.sunIntensity, t) * sunUp;
    const f32 moonPower = blend(a.moonIntensity, b.moonIntensity, t) * moonUp;
    if (sunPower >= moonPower)
    {
        env.sunDirection = sun;
        env.sunColor = blend(a.sunColor, b.sunColor, t);
        env.sunIntensity = sunPower;
    }
    else
    {
        env.sunDirection = moon;
        env.sunColor = blend(a.moonColor, b.moonColor, t);
        env.sunIntensity = moonPower;
    }
    env.ambientSky = blend(a.ambientSky, b.ambientSky, t);
    env.ambientGround = blend(a.ambientGround, b.ambientGround, t);
    env.fogColor = blend(a.fogColor, b.fogColor, t);
    env.fogDensity = fogDensity * blend(a.fogDensity, b.fogDensity, t);

    render::Sky& sky = sample.sky;
    sky.zenith = blend(a.skyZenith, b.skyZenith, t);
    sky.sunDirection = sun;
    sky.sunColor = blend(a.sunColor, b.sunColor, t) * sunUp;
    sky.moonDirection = moon;
    sky.moon = moonUp;
    sky.stars = blend(a.stars, b.stars, t);
    return sample;
}
} // namespace g7::world

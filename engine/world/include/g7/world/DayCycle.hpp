#pragma once

// Day and night (M4): light, sky and fog by time of day from data curves (data/environment.toml,
// world.md "Spielzeit & Umgebung"). The values are content - the engine only interpolates them.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Lighting.hpp>

#include <string_view>
#include <vector>

namespace g7::asset
{
class Vfs;
}

namespace g7::world
{
/// One key of the curves. Colours are linear (the file holds sRGB 0..1, converted when read).
struct EnvironmentKey
{
    f32 hour = 0.0f; ///< 0 .. 24
    Vec3 sunColor{1.0f};
    f32 sunIntensity = 1.0f;
    Vec3 moonColor{0.5f, 0.6f, 0.9f};
    f32 moonIntensity = 0.0f;
    Vec3 ambientSky{0.2f};
    Vec3 ambientGround{0.1f};
    Vec3 fogColor{0.1f};
    f32 fogDensity = 1.0f;            ///< factor on [render] fog_density
    Vec3 skyZenith{0.1f, 0.2f, 0.5f}; ///< the horizon is the fog colour (distant geometry fades into the sky)
    f32 stars = 0.0f;                 ///< 0 .. 1
};

/// Paths of sun and moon. The sun rises in the east (+X), stands south (+Z) at noon and sets in the
/// west (-X); the moon runs opposite.
struct Orbit
{
    f32 sunrise = 6.0f; ///< hours
    f32 sunset = 18.0f;
    f32 noonElevation = 60.0f;                                 ///< degrees above the southern horizon
    f32 moonElevation = 45.0f;                                 ///< degrees, at midnight
    [[nodiscard]] Vec3 sunDirection(f32 hour) const noexcept;  ///< towards the sun, normalised
    [[nodiscard]] Vec3 moonDirection(f32 hour) const noexcept; ///< towards the moon
};

/// Everything the renderer needs for one moment.
struct DaySample
{
    render::Environment environment; ///< directional light = sun by day, moon by night
    render::Sky sky;
};

class DayCycle
{
public:
    /// Reads the TOML text; errors name `source` and the key ("environment.toml: key[2].hour: ...").
    [[nodiscard]] static Result<DayCycle> parse(std::string_view toml, std::string_view source);
    [[nodiscard]] static Result<DayCycle> load(const asset::Vfs& vfs, std::string_view path);
    /// Built-in fallback (dusk-like, as before M4) when no file is there.
    [[nodiscard]] static DayCycle fallback();

    /// Curves at `hour` (0 .. 24, cyclic), smoothly between the keys. `fogDensity` is the configured
    /// base density the key factors scale.
    [[nodiscard]] DaySample evaluate(f32 hour, f32 fogDensity) const;

    [[nodiscard]] const Orbit& orbit() const noexcept { return m_orbit; }
    [[nodiscard]] const std::vector<EnvironmentKey>& keys() const noexcept { return m_keys; }

private:
    Orbit m_orbit;
    std::vector<EnvironmentKey> m_keys; // sorted by hour, at least one
};
} // namespace g7::world

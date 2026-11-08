#pragma once

// Weather (M17 part E1, docs/design/wetter.md): states with rain, clouds, wind and mood from
// data/weather.toml, a deterministic schedule over the game time (durations, weighted successors, start
// windows by hour) and fades between them. The state mixes into the time of day (light, fog, sky, colour
// grading); rain, wetness, lightning and wind read it in the later parts.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Grading.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace g7::asset
{
class Vfs;
}

namespace g7::world
{
struct DaySample;

/// One weather state: `[state.<name>]` in weather.toml.
struct WeatherState
{
    std::string name;
    f32 rain = 0.0f;                 ///< 0 dry .. 1 heavy rain
    f32 clouds = 0.0f;               ///< 0 clear .. 1 overcast (greys the sky, hides moon and stars)
    f32 wind = 0.2f;                 ///< 0 still .. 1 storm
    f32 gusts = 0.1f;                ///< how much the wind varies
    f32 fog = 1.0f;                  ///< factor on the fog density of the time of day
    f32 sun = 1.0f;                  ///< factor on the sun's (or moon's) light
    f32 ambient = 1.0f;              ///< factor on the ambient light
    f32 shadow = 1.0f;               ///< how dark the sun's shadows stay, 1 full .. 0 none (overcast)
    f32 lightning = 0.0f;            ///< flashes per minute (part G)
    render::GradingOverride grading; ///< over the time of day's grading
    f32 minHours = 1.0f;             ///< duration in game hours
    f32 maxHours = 3.0f;
    std::optional<std::pair<f32, f32>> window;     ///< may only begin between these hours
    std::vector<std::pair<std::string, f32>> next; ///< what may follow, with weights
};

/// The weather of a moment: the states mixed over the fade.
struct WeatherSample
{
    std::string_view state; ///< the current state (the fade's target)
    f32 rain = 0.0f;
    f32 clouds = 0.0f;
    f32 wind = 0.0f;
    f32 gusts = 0.0f;
    Vec2 windDirection{1.0f, 0.0f}; ///< M17 W: where the wind blows to (unit, world xz); turns slowly
    f32 fog = 1.0f;
    f32 sun = 1.0f;
    f32 ambient = 1.0f;
    f32 shadow = 1.0f;
    f32 lightning = 0.0f;
    f32 wetness =
        0.0f;        ///< M17 N: how wet the world is outside, 0 dry .. 1 soaked (rises in rain, dries after)
    f32 fade = 1.0f; ///< 0 at the change .. 1 when the new state is reached
};

/// A flash of lightning (M17 part G): when (real seconds of the engine's clock), how far (metres) and from
/// where.
struct Lightning
{
    f64 start = 0.0;
    f32 distance = 1000.0f;
    f32 azimuth = 0.0f; ///< radians about +Y
    u32 flickers = 2;   ///< how often it flares within the flash
};

/// M17 W: where the wind blows to at game `clock` (minutes): mostly from the west (towards +X, a little +Z),
/// turning slowly by up to about 45 degrees either way over a few game hours. Depends on the clock only, so
/// it is the same after loading.
[[nodiscard]] Vec2 windDirection(f64 clock) noexcept;

inline constexpr f32 kSpeedOfSound = 343.0f; ///< m/s
/// Seconds from the flash to its thunder.
[[nodiscard]] inline f32 thunderDelay(f32 metres) noexcept
{
    return metres / kSpeedOfSound;
}
/// Brightness 0..1 of a flash `seconds` after its start: `flickers` short flares within about 0.25 s, the
/// later ones weaker; 0 before and afterwards.
[[nodiscard]] f32 lightningBrightness(f32 seconds, u32 flickers) noexcept;
/// How strongly a flash lights the world, by its distance (near ones brighter; decision 8: clear, not
/// glaring).
[[nodiscard]] f32 lightningStrength(f32 distance) noexcept;

/// The flashes of a thunderstorm: at random (a Poisson process over the game time, `perMinute` flashes per
/// game minute), each at a random distance (300 .. 3000 m, near ones rarer) and direction. Deterministic by
/// its seed.
class LightningStorm
{
public:
    explicit LightningStorm(u64 seed = 0x7a1e5u) noexcept : m_random(seed) {}
    /// Advances by `gameMinutes` at `perMinute` flashes per game minute; a flash begins at `now` (real
    /// seconds).
    std::optional<Lightning> step(f64 gameMinutes, f32 perMinute, f64 now) noexcept;

private:
    [[nodiscard]] f32 uniform() noexcept;
    u64 m_random;
};

/// What a save game keeps (save::WeatherSave mirrors it).
struct WeatherSnapshot
{
    std::string state;
    std::string previous;
    f64 changeMinute = 0.0; ///< game clock (minutes since day 0) of the last change
    f64 fadeMinutes = 0.0;
    f64 nextMinute = 0.0; ///< when the schedule decides again
    u64 random = 0;
    bool locked = false;
    f32 wetness = 0.0f;     ///< M17 N
    f64 wetnessClock = 0.0; ///< game clock the wetness was last advanced to
};

class Weather
{
public:
    /// Errors name `source` and the entry ("weather.toml: state.regen.next: unknown state 'x'").
    [[nodiscard]] static Result<Weather> parse(std::string_view toml, std::string_view source);
    [[nodiscard]] static Result<Weather> load(const asset::Vfs& vfs, std::string_view path);
    /// Without the file: always clear, nothing changes.
    [[nodiscard]] static Weather fallback();

    /// Begins with the file's start state at `clock`; `seed` fixes the whole course.
    void start(f64 clock, u64 seed);
    /// Advances the schedule to `clock`. `hold`: no change now (dialogue, cut scene; decision 4) - it waits.
    /// Returns the changes made (old, new), oldest first.
    std::vector<std::pair<std::string, std::string>> update(f64 clock, bool hold = false);
    /// Sets a state now, fading over `fadeMinutes` (0: at once); false for an unknown name. The schedule goes
    /// on from it.
    bool set(std::string_view name, f64 clock, f64 fadeMinutes);
    /// A story scene keeps the state: no changes by the schedule while locked (set() still works).
    void lock(bool locked) noexcept { m_locked = locked; }
    /// As if the current state had lasted a while (--weather for screenshots): wet in rain, dry otherwise.
    void settle() noexcept { m_wetness = m_states[m_current].rain > 0.02f ? 1.0f : 0.0f; }
    [[nodiscard]] bool locked() const noexcept { return m_locked; }

    [[nodiscard]] WeatherSample sample(f64 clock) const;
    /// Mixes the weather at `clock` into the time of day: sun and ambient light, fog density, a greyer sky
    /// and fog colour under clouds, moon and stars hidden, the states' colour grading.
    void apply(DaySample& day, f64 clock) const;

    [[nodiscard]] const std::string& current() const;
    [[nodiscard]] f64 nextChange() const noexcept { return m_nextMinute; }
    [[nodiscard]] const std::vector<WeatherState>& states() const noexcept { return m_states; }
    [[nodiscard]] const WeatherState* find(std::string_view name) const;

    [[nodiscard]] WeatherSnapshot snapshot() const;
    /// False (and nothing changed) if a state is unknown to this weather.toml.
    bool restore(const WeatherSnapshot& snapshot);

private:
    void schedule(f64 from);
    [[nodiscard]] u64 nextRandom() noexcept;
    [[nodiscard]] f32 uniform() noexcept;

    std::vector<WeatherState> m_states;
    usize m_start = 0;
    f64 m_fadeMinutes = 15.0; ///< transition_minutes
    usize m_current = 0;
    usize m_previous = 0;
    f64 m_changeMinute = 0.0;
    f64 m_changeFade = 0.0; ///< the fade of the last change
    f64 m_nextMinute = 1e300;
    u64 m_random = 0x9e3779b97f4a7c15ull;
    bool m_locked = false;
    f32 m_wetness = 0.0f;
    f64 m_wetnessClock = 0.0;
    void advanceWetness(f64 clock) noexcept;
};
} // namespace g7::world

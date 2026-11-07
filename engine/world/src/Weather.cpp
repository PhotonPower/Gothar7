#include <g7/asset/Vfs.hpp>
#include <g7/core/Config.hpp>
#include <g7/world/DayCycle.hpp>
#include <g7/world/Weather.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7::world
{
namespace
{
constexpr Vec3 kLuma{0.2126f, 0.7152f, 0.0722f};

/// grading_* of a state (as DayCycle reads them for [zone.<name>]); error text or empty.
std::string readGrading(const Config& c, const std::string& at, render::GradingOverride& out)
{
    for (auto [name, target] :
         {std::pair{"grading_temperature", &out.temperature}, std::pair{"grading_tint", &out.tint},
          std::pair{"grading_saturation", &out.saturation}, std::pair{"grading_contrast", &out.contrast},
          std::pair{"vignette", &out.vignette}})
    {
        if (const auto value = c.find<f64>(at + "." + name))
        {
            *target = static_cast<f32>(*value);
        }
        else if (c.contains(at + "." + name))
        {
            return std::string(name) + ": must be a number";
        }
    }
    for (auto [name, target] : {std::pair{"grading_lift", &out.lift}, std::pair{"grading_gamma", &out.gamma},
                                std::pair{"grading_gain", &out.gain}})
    {
        if (const auto value = c.find<std::vector<f64>>(at + "." + name); value && value->size() == 3)
        {
            *target = Vec3((*value)[0], (*value)[1], (*value)[2]);
        }
        else if (c.contains(at + "." + name))
        {
            return std::string(name) + ": must be [r, g, b]";
        }
    }
    return {};
}

f64 hourOf(f64 clock)
{
    return std::fmod(clock, 1440.0) / 60.0;
}

bool inWindow(const WeatherState& s, f64 clock)
{
    if (!s.window)
    {
        return true;
    }
    const f64 h = hourOf(clock);
    const auto [from, to] = *s.window;
    return from <= to ? (h >= from && h < to) : (h >= from || h < to); // over midnight
}
} // namespace

Result<Weather> Weather::parse(std::string_view toml, std::string_view source)
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
    Weather weather;
    weather.m_fadeMinutes = c.get<f64>("transition_minutes", 15.0);
    if (weather.m_fadeMinutes < 0.0)
    {
        return error("transition_minutes", "must not be negative");
    }
    std::vector<std::string> names = c.keys("state");
    std::sort(names.begin(), names.end()); // stable order: the course must not depend on the TOML library
    if (names.empty())
    {
        return error("state", "needs at least one [state.<name>]");
    }
    for (const std::string& name : names)
    {
        const std::string at = "state." + name;
        WeatherState s;
        s.name = name;
        for (auto [key, target] :
             {std::pair{"rain", &s.rain}, std::pair{"clouds", &s.clouds}, std::pair{"wind", &s.wind},
              std::pair{"gusts", &s.gusts}, std::pair{"fog", &s.fog}, std::pair{"sun", &s.sun},
              std::pair{"ambient", &s.ambient}, std::pair{"shadow", &s.shadow},
              std::pair{"lightning", &s.lightning}})
        {
            if (const auto value = c.find<f64>(at + "." + key))
            {
                *target = static_cast<f32>(*value);
            }
            else if (c.contains(at + "." + key))
            {
                return error(at, std::string(key) + " must be a number");
            }
        }
        for (const f32 v : {s.rain, s.clouds, s.wind, s.gusts})
        {
            if (v < 0.0f || v > 1.0f)
            {
                return error(at, "rain, clouds, wind and gusts must lie in 0..1");
            }
        }
        if (s.fog < 0.0f || s.sun < 0.0f || s.ambient < 0.0f || s.lightning < 0.0f)
        {
            return error(at, "fog, sun, ambient and lightning must not be negative");
        }
        if (s.shadow < 0.0f || s.shadow > 1.0f)
        {
            return error(at, "shadow must lie in 0..1");
        }
        if (const std::string bad = readGrading(c, at, s.grading); !bad.empty())
        {
            return error(at, bad);
        }
        if (const auto d = c.find<std::vector<f64>>(at + ".duration");
            d && d->size() == 2 && (*d)[0] > 0.0 && (*d)[0] <= (*d)[1])
        {
            s.minHours = static_cast<f32>((*d)[0]);
            s.maxHours = static_cast<f32>((*d)[1]);
        }
        else if (c.contains(at + ".duration"))
        {
            return error(at, "duration must be [min, max] game hours, 0 < min <= max");
        }
        if (const auto w = c.find<std::vector<f64>>(at + ".window"); w && w->size() == 2)
        {
            s.window = std::pair{static_cast<f32>((*w)[0]), static_cast<f32>((*w)[1])};
        }
        else if (c.contains(at + ".window"))
        {
            return error(at, "window must be [from, to] hours");
        }
        std::vector<std::string> next = c.keys(at + ".next");
        std::sort(next.begin(), next.end());
        for (const std::string& n : next)
        {
            const auto weight = c.find<f64>(at + ".next." + n);
            if (!weight || *weight < 0.0)
            {
                return error(at + ".next." + n, "must be a weight >= 0");
            }
            if (!std::binary_search(names.begin(), names.end(), n))
            {
                return error(at + ".next", "unknown state '" + n + "'");
            }
            s.next.emplace_back(n, static_cast<f32>(*weight));
        }
        weather.m_states.push_back(std::move(s));
    }
    const std::string start = c.get<std::string>("start", weather.m_states.front().name);
    const WeatherState* first = weather.find(start);
    if (first == nullptr)
    {
        return error("start", "unknown state '" + start + "'");
    }
    weather.m_start = static_cast<usize>(first - weather.m_states.data());
    weather.m_current = weather.m_previous = weather.m_start;
    return weather;
}

Result<Weather> Weather::load(const asset::Vfs& vfs, std::string_view path)
{
    auto bytes = vfs.read(path);
    if (!bytes)
    {
        return bytes.error();
    }
    return parse(std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()),
                 path);
}

Weather Weather::fallback()
{
    Weather weather;
    WeatherState clear;
    clear.name = "klar";
    weather.m_states.push_back(clear);
    return weather;
}

const WeatherState* Weather::find(std::string_view name) const
{
    const auto it =
        std::find_if(m_states.begin(), m_states.end(), [&](const WeatherState& s) { return s.name == name; });
    return it == m_states.end() ? nullptr : &*it;
}

const std::string& Weather::current() const
{
    return m_states[m_current].name;
}

u64 Weather::nextRandom() noexcept
{
    // splitmix64: small, fast, the same on every platform.
    u64 z = (m_random += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

f32 Weather::uniform() noexcept
{
    return static_cast<f32>(nextRandom() >> 40) / static_cast<f32>(1ull << 24);
}

void Weather::start(f64 clock, u64 seed)
{
    m_random = seed;
    m_current = m_previous = m_start;
    m_changeMinute = clock;
    m_changeFade = 0.0;
    m_wetness = 0.0f;
    m_wetnessClock = clock;
    schedule(clock);
}

void Weather::advanceWetness(f64 clock) noexcept
{
    // M17 N: wet within about 10 game minutes of steady rain (faster in heavy rain), dry about an hour after
    // it.
    const f64 minutes = clock - m_wetnessClock;
    m_wetnessClock = clock;
    if (minutes <= 0.0)
    {
        return;
    }
    const f32 rain = sample(clock).rain;
    const f32 rate = rain > 0.02f ? std::min(1.0f, rain * 2.0f) / 10.0f : -1.0f / 60.0f; // per game minute
    m_wetness = std::clamp(m_wetness + rate * static_cast<f32>(minutes), 0.0f, 1.0f);
}

void Weather::schedule(f64 from)
{
    const WeatherState& s = m_states[m_current];
    const f32 hours = s.minHours + (s.maxHours - s.minHours) * uniform();
    m_nextMinute = from + static_cast<f64>(hours) * 60.0;
}

std::vector<std::pair<std::string, std::string>> Weather::update(f64 clock, bool hold)
{
    std::vector<std::pair<std::string, std::string>> changes;
    advanceWetness(clock);
    if (m_states.size() < 2)
    {
        return changes;
    }
    for (int guard = 0; clock >= m_nextMinute && guard < 1000; ++guard)
    {
        if (hold || m_locked)
        {
            m_nextMinute = clock + 1.0; // try again in a minute
            break;
        }
        const f64 at = m_nextMinute;
        const WeatherState& now = m_states[m_current];
        f32 total = 0.0f;
        for (const auto& [name, weight] : now.next)
        {
            total += inWindow(*find(name), at) ? weight : 0.0f;
        }
        usize chosen = m_current;
        if (total > 0.0f)
        {
            f32 pick = uniform() * total;
            for (const auto& [name, weight] : now.next)
            {
                const WeatherState* candidate = find(name);
                if (!inWindow(*candidate, at) || weight <= 0.0f)
                {
                    continue;
                }
                chosen = static_cast<usize>(candidate - m_states.data());
                pick -= weight;
                if (pick < 0.0f)
                {
                    break;
                }
            }
        }
        if (chosen != m_current)
        {
            changes.emplace_back(now.name, m_states[chosen].name);
            m_previous = m_current;
            m_current = chosen;
            m_changeMinute = at;
            m_changeFade = m_fadeMinutes;
        }
        schedule(at);
    }
    return changes;
}

bool Weather::set(std::string_view name, f64 clock, f64 fadeMinutes)
{
    const WeatherState* state = find(name);
    if (state == nullptr)
    {
        return false;
    }
    m_previous = m_current;
    m_current = static_cast<usize>(state - m_states.data());
    m_changeMinute = clock;
    m_changeFade = std::max(fadeMinutes, 0.0);
    schedule(clock);
    return true;
}

WeatherSample Weather::sample(f64 clock) const
{
    const WeatherState& a = m_states[m_previous];
    const WeatherState& b = m_states[m_current];
    const f32 t = m_changeFade <= 0.0
                      ? 1.0f
                      : static_cast<f32>(std::clamp((clock - m_changeMinute) / m_changeFade, 0.0, 1.0));
    const auto mix = [t](f32 x, f32 y) { return x + (y - x) * t; };
    WeatherSample s;
    s.state = b.name;
    s.rain = mix(a.rain, b.rain);
    s.clouds = mix(a.clouds, b.clouds);
    s.wind = mix(a.wind, b.wind);
    s.gusts = mix(a.gusts, b.gusts);
    s.fog = mix(a.fog, b.fog);
    s.sun = mix(a.sun, b.sun);
    s.ambient = mix(a.ambient, b.ambient);
    s.shadow = mix(a.shadow, b.shadow);
    s.lightning = mix(a.lightning, b.lightning);
    s.fade = t;
    s.wetness = m_wetness;
    return s;
}

void Weather::apply(DaySample& day, f64 clock) const
{
    const WeatherSample s = sample(clock);
    render::Environment& e = day.environment;
    e.sunIntensity *= s.sun;
    e.ambientSky *= s.ambient;
    e.ambientGround *= s.ambient;
    e.fogDensity *= s.fog;
    e.shadowStrength *= s.shadow;
    // Clouds: sky and fog turn grey (keeping their brightness), moon and stars vanish.
    const auto grey = [](const Vec3& c, f32 amount) { return glm::mix(c, Vec3(glm::dot(c, kLuma)), amount); };
    day.sky.zenith = grey(day.sky.zenith, 0.8f * s.clouds);
    e.fogColor = grey(e.fogColor, 0.6f * s.clouds);
    day.sky.moon *= 1.0f - s.clouds;
    day.sky.stars *= 1.0f - s.clouds;
    // The grading: each state over the time of day, mixed over the fade.
    const render::Grading from = render::applyOverride(day.grading, m_states[m_previous].grading);
    const render::Grading to = render::applyOverride(day.grading, m_states[m_current].grading);
    day.grading = render::mixGrading(from, to, s.fade);
}

WeatherSnapshot Weather::snapshot() const
{
    return {m_states[m_current].name,
            m_states[m_previous].name,
            m_changeMinute,
            m_changeFade,
            m_nextMinute,
            m_random,
            m_locked,
            m_wetness,
            m_wetnessClock};
}

bool Weather::restore(const WeatherSnapshot& snapshot)
{
    const WeatherState* current = find(snapshot.state);
    const WeatherState* previous = find(snapshot.previous);
    if (current == nullptr || previous == nullptr)
    {
        return false;
    }
    m_current = static_cast<usize>(current - m_states.data());
    m_previous = static_cast<usize>(previous - m_states.data());
    m_changeMinute = snapshot.changeMinute;
    m_changeFade = snapshot.fadeMinutes;
    m_nextMinute = snapshot.nextMinute;
    m_random = snapshot.random;
    m_locked = snapshot.locked;
    m_wetness = snapshot.wetness;
    m_wetnessClock = snapshot.wetnessClock;
    return true;
}
} // namespace g7::world

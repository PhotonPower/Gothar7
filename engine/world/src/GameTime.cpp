#include <g7/world/GameTime.hpp>

#include <algorithm>
#include <cmath>

namespace g7::world
{
GameTime::GameTime(f64 secondsPerMinute) noexcept
{
    setSecondsPerMinute(secondsPerMinute);
}

void GameTime::setSecondsPerMinute(f64 seconds) noexcept
{
    m_secondsPerMinute = seconds > 0.0 ? seconds : 4.0;
}

void GameTime::advance(f64 seconds)
{
    if (seconds <= 0.0)
    {
        return;
    }
    const u64 before = totalMinutes();
    m_minutes += seconds / m_secondsPerMinute;
    const u64 after = totalMinutes();
    if (!m_callback || after == before)
    {
        return;
    }
    if (after - before > kMaxMinuteEvents)
    {
        m_callback({TimeEvent::Kind::Jumped, before, after});
        return;
    }
    for (u64 minute = before + 1; minute <= after; ++minute)
    {
        m_callback({TimeEvent::Kind::Minute, minute - 1, minute});
    }
}

void GameTime::jumpTo(f64 minutes)
{
    const u64 before = totalMinutes();
    m_minutes = std::max(minutes, 0.0);
    if (m_callback)
    {
        m_callback({TimeEvent::Kind::Jumped, before, totalMinutes()});
    }
}

void GameTime::setTime(u32 day, u32 hour, u32 minute)
{
    jumpTo(static_cast<f64>(day) * kMinutesPerDay + std::min(hour, 23u) * 60.0 + std::min(minute, 59u));
}

void GameTime::advanceTo(u32 hour, u32 minute)
{
    const f64 target = std::min(hour, 23u) * 60.0 + std::min(minute, 59u);
    const f64 today = static_cast<f64>(day()) * kMinutesPerDay;
    jumpTo(today + target > m_minutes ? today + target : today + kMinutesPerDay + target);
}

u32 GameTime::day() const noexcept
{
    return static_cast<u32>(m_minutes / kMinutesPerDay);
}

f64 GameTime::minuteOfDay() const noexcept
{
    return std::fmod(m_minutes, static_cast<f64>(kMinutesPerDay));
}

f32 GameTime::hourOfDay() const noexcept
{
    return static_cast<f32>(minuteOfDay() / 60.0);
}

u64 GameTime::totalMinutes() const noexcept
{
    return static_cast<u64>(std::floor(m_minutes));
}
} // namespace g7::world

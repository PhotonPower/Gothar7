#pragma once

// Game time (M4): day and minute, advanced with the fixed simulation step. Global - it keeps running
// across level changes and belongs into the save game (save.md).

#include <g7/core/Types.hpp>

#include <functional>
#include <utility>

namespace g7::world
{
inline constexpr u32 kMinutesPerDay = 24 * 60;

/// What the clock reports. Minute events come one per game minute in increasing order; a jump
/// (advanceTo, setTime, sleeping, or more than kMaxMinuteEvents minutes in one advance) is one event
/// instead - listeners (NPC routines, M9) then put everything where it belongs at `to`, as Gothic
/// does after sleeping.
struct TimeEvent
{
    enum class Kind : u8
    {
        Minute, ///< the clock reached minute `to` (from = to - 1)
        Jumped, ///< the clock jumped from `from` to `to`
    };
    Kind kind = Kind::Minute;
    u64 from = 0; ///< total minutes since day 0, 00:00
    u64 to = 0;
};

class GameTime
{
public:
    using Callback = std::function<void(const TimeEvent&)>;
    /// More minutes than this in one advance() are reported as a jump.
    static constexpr u64 kMaxMinuteEvents = 60;

    /// `secondsPerMinute`: real seconds per game minute (engine.toml [time] minute_seconds; 4 = a day in 96
    /// min).
    explicit GameTime(f64 secondsPerMinute = 4.0) noexcept;

    void setCallback(Callback callback) { m_callback = std::move(callback); }
    void setSecondsPerMinute(f64 seconds) noexcept;
    [[nodiscard]] f64 secondsPerMinute() const noexcept { return m_secondsPerMinute; }

    /// Simulation step of `seconds` real time (scaled by secondsPerMinute).
    void advance(f64 seconds);
    /// Sets day and time of day directly (loading, debugging); reported as a jump.
    void setTime(u32 day, u32 hour, u32 minute = 0);
    /// Jumps forward to the next hour:minute (sleeping: "until 8 o'clock"); today if still ahead,
    /// otherwise tomorrow. One Jumped event.
    void advanceTo(u32 hour, u32 minute = 0);

    [[nodiscard]] u32 day() const noexcept;
    /// Minute of the day with fraction, 0 .. 1440.
    [[nodiscard]] f64 minuteOfDay() const noexcept;
    /// Hour of the day with fraction, 0 .. 24 (curves, sky).
    [[nodiscard]] f32 hourOfDay() const noexcept;
    /// Whole minutes since day 0, 00:00.
    [[nodiscard]] u64 totalMinutes() const noexcept;
    /// Exact clock (save game): minutes since day 0 with fraction.
    [[nodiscard]] f64 clock() const noexcept { return m_minutes; }
    void restore(f64 clock) noexcept { m_minutes = clock; }

private:
    void jumpTo(f64 minutes);

    f64 m_minutes = 8.0 * 60.0; ///< since day 0, 00:00; starts at 08:00
    f64 m_secondsPerMinute = 4.0;
    Callback m_callback;
};
} // namespace g7::world

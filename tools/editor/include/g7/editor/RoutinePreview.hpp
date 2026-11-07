#pragma once

// Routine preview (M16 part E, docs/modules/editor.md): which entry of a routine holds at a minute of the
// day, and the checks of a day plan (times, gaps, overlaps, places the waynet does not know) - from the
// loaded scripts' Routine instances, without running the AI.

#include <g7/runtime/Engine.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::editor
{
/// "08:30" -> minute of the day (0 .. 1440; "24:00" = 1440); nullopt if malformed (as the engine reads them).
[[nodiscard]] std::optional<u32> parseMinute(std::string_view text) noexcept;
/// Minute of the day -> "08:30".
[[nodiscard]] std::string minuteText(u32 minute);

/// The entry whose window [from, to) holds `minute` (across midnight when to <= from); the first such entry,
/// as the engine takes it. nullopt: none.
[[nodiscard]] std::optional<usize> entryAt(const RoutineInfo& routine, u32 minute);

struct RoutineIssue
{
    std::string routine;
    std::string text;
};

/// Is this a known place (way point or freepoint)?
using PlaceKnownFn = std::function<bool(std::string_view at)>;

/// The issues of a routine: malformed times, empty windows, the minutes of the day no entry holds (as ranges)
/// and those several hold, entries without state or place, and places `known` does not know.
[[nodiscard]] std::vector<RoutineIssue> checkRoutine(const RoutineInfo& routine, const PlaceKnownFn& known);
} // namespace g7::editor

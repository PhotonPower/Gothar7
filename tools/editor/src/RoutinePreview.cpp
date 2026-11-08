#include <g7/editor/RoutinePreview.hpp>

#include <array>
#include <format>

namespace g7::editor
{
namespace
{
constexpr u32 kDay = 24 * 60;

bool inWindow(u32 minute, u32 from, u32 to) noexcept
{
    return from < to ? (minute >= from && minute < to) : (minute >= from || minute < to);
}

/// Runs of equal `count` condition as "08:00-09:30" texts.
std::vector<std::string> runs(const std::array<u8, kDay>& counts, const std::function<bool(u8)>& wanted)
{
    std::vector<std::string> out;
    u32 m = 0;
    while (m < kDay)
    {
        if (!wanted(counts[m]))
        {
            ++m;
            continue;
        }
        const u32 start = m;
        while (m < kDay && wanted(counts[m]))
        {
            ++m;
        }
        out.push_back(std::format("{}-{}", minuteText(start), minuteText(m)));
    }
    return out;
}
} // namespace

std::optional<u32> parseMinute(std::string_view text) noexcept
{
    if (text.size() != 5 || text[2] != ':')
    {
        return std::nullopt;
    }
    for (const usize i : {0u, 1u, 3u, 4u})
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return std::nullopt;
        }
    }
    const u32 hours = static_cast<u32>(text[0] - '0') * 10 + static_cast<u32>(text[1] - '0');
    const u32 minutes = static_cast<u32>(text[3] - '0') * 10 + static_cast<u32>(text[4] - '0');
    if (hours > 24 || minutes > 59 || (hours == 24 && minutes != 0))
    {
        return std::nullopt;
    }
    return hours * 60 + minutes;
}

std::string minuteText(u32 minute)
{
    return std::format("{:02}:{:02}", minute / 60, minute % 60);
}

std::optional<usize> entryAt(const RoutineInfo& routine, u32 minute)
{
    minute %= kDay;
    for (usize i = 0; i < routine.entries.size(); ++i)
    {
        const auto from = parseMinute(routine.entries[i].from);
        const auto to = parseMinute(routine.entries[i].to);
        if (from && to && inWindow(minute, *from, *to % kDay))
        {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<RoutineIssue> checkRoutine(const RoutineInfo& routine, const PlaceKnownFn& known)
{
    std::vector<RoutineIssue> issues;
    const auto add = [&](std::string text) { issues.push_back({routine.name, std::move(text)}); };
    std::array<u8, kDay> counts{};
    for (usize i = 0; i < routine.entries.size(); ++i)
    {
        const RoutineEntryInfo& e = routine.entries[i];
        const auto from = parseMinute(e.from);
        const auto to = parseMinute(e.to);
        if (!from || !to)
        {
            add(std::format("entry {}: time '{}'-'{}' is not HH:MM", i + 1, e.from, e.to));
            continue;
        }
        if (*from % kDay == *to % kDay)
        {
            add(std::format("entry {}: {}-{} is empty", i + 1, e.from, e.to));
            continue;
        }
        for (u32 m = 0; m < kDay; ++m)
        {
            if (inWindow(m, *from % kDay, *to % kDay) && counts[m] < 255)
            {
                ++counts[m];
            }
        }
        if (e.state.empty())
        {
            add(std::format("entry {} ({}-{}): no state", i + 1, e.from, e.to));
        }
        if (e.at.empty())
        {
            add(std::format("entry {} ({}-{}): no place", i + 1, e.from, e.to));
        }
        else if (known && !known(e.at))
        {
            add(std::format("entry {} ({}-{}): the waynet has no '{}'", i + 1, e.from, e.to, e.at));
        }
    }
    if (routine.entries.empty())
    {
        add("no entries");
        return issues;
    }
    for (const std::string& gap : runs(counts, [](u8 c) { return c == 0; }))
    {
        add("nothing to do " + gap);
    }
    for (const std::string& overlap : runs(counts, [](u8 c) { return c > 1; }))
    {
        add("entries overlap " + overlap + " (the first counts)");
    }
    return issues;
}
} // namespace g7::editor

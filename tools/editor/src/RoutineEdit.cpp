#include <g7/core/StringUtil.hpp>
#include <g7/editor/RoutineEdit.hpp>
#include <g7/editor/RoutinePreview.hpp>

#include <algorithm>
#include <format>
#include <regex>

namespace g7::editor
{
namespace
{
/// Text the file can hold without escaping (identifiers and times).
bool plain(std::string_view s) noexcept
{
    return std::ranges::all_of(s,
                               [](char c)
                               {
                                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                          (c >= '0' && c <= '9') || c == '_' || c == ':';
                               });
}
} // namespace

std::string editorRoutineFile(std::string_view site)
{
    return std::format("routines/{}_editor.lua", site);
}

std::string siteOf(std::string_view worldPath)
{
    const usize slash = worldPath.find_last_of("/\\");
    std::string_view file = slash == std::string_view::npos ? worldPath : worldPath.substr(slash + 1);
    file = file.substr(0, file.find('.'));
    return toLower(file);
}

bool validRoutineName(std::string_view name) noexcept
{
    return name.size() > 4 && name.starts_with("rtn_") &&
           std::ranges::all_of(name, [](char c)
                               { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

void sortEntries(RoutineInfo& routine)
{
    std::ranges::stable_sort(routine.entries, {},
                             [](const RoutineEntryInfo& e) { return parseMinute(e.from).value_or(100000u); });
}

std::string writeRoutines(std::span<const RoutineInfo> routines, std::string_view site)
{
    std::vector<RoutineInfo> sorted(routines.begin(), routines.end());
    std::ranges::sort(sorted, {}, &RoutineInfo::name);
    std::string out =
        std::format("-- Written by the Gothar editor (M16): the routines of the site \"{}\".\n"
                    "-- Do not edit by hand - the editor writes this file anew. Hand-written routines "
                    "belong in other files.\n",
                    site);
    for (RoutineInfo& r : sorted)
    {
        sortEntries(r);
        out += std::format("\nRoutine \"{}\" {{\n", r.name);
        for (const RoutineEntryInfo& e : r.entries)
        {
            out += std::format("    {{ from = \"{}\", to = \"{}\", state = \"{}\", at = \"{}\" }},\n", e.from,
                               e.to, e.state, e.at);
        }
        out += "}\n";
    }
    return out;
}

Result<std::vector<RoutineInfo>> parseRoutines(std::string_view text, std::string_view file)
{
    static const std::regex kStart(R"re(^Routine "([a-z0-9_]+)" \{$)re");
    static const std::regex kEntry(
        R"re(^ *\{ from = "([^"]*)", to = "([^"]*)", state = "([^"]*)", at = "([^"]*)" \},$)re");
    std::vector<RoutineInfo> routines;
    bool open = false;
    usize lineNo = 0;
    usize start = 0;
    while (start <= text.size())
    {
        const usize end = std::min(text.find('\n', start), text.size());
        std::string line(text.substr(start, end - start));
        start = end + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        std::smatch m;
        if (line.empty() || line.starts_with("--"))
        {
            if (end == text.size())
            {
                break;
            }
            continue;
        }
        if (!open && std::regex_match(line, m, kStart))
        {
            routines.push_back({m[1].str(), std::string(file), {}});
            open = true;
        }
        else if (open && line == "}")
        {
            open = false;
        }
        else if (open && std::regex_match(line, m, kEntry))
        {
            routines.back().entries.push_back({m[1].str(), m[2].str(), m[3].str(), m[4].str()});
        }
        else
        {
            return Error{std::format("{}:{}: not a line the editor writes: {}", file, lineNo, line)};
        }
        if (end == text.size())
        {
            break;
        }
    }
    if (open)
    {
        return Error{std::format("{}: the last Routine is not closed", file)};
    }
    for (const RoutineInfo& r : routines)
    {
        for (const RoutineEntryInfo& e : r.entries)
        {
            if (!plain(e.from) || !plain(e.to) || !plain(e.state) || !plain(e.at))
            {
                return Error{std::format("{}: {} holds characters the editor does not write", file, r.name)};
            }
        }
    }
    return routines;
}
} // namespace g7::editor

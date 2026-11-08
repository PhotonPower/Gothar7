#include "Licences.hpp"

#include <g7/core/Config.hpp>

#include <algorithm>
#include <filesystem>
#include <format>

namespace g7::cook
{
namespace
{
bool matchFrom(std::string_view p, std::string_view s) noexcept
{
    while (!p.empty())
    {
        if (p.starts_with("**"))
        {
            p.remove_prefix(2);
            if (p.starts_with("/"))
            {
                p.remove_prefix(1);
            }
            for (usize i = 0; i <= s.size(); ++i) // any number of characters, '/' included
            {
                if (matchFrom(p, s.substr(i)))
                {
                    return true;
                }
            }
            return false;
        }
        if (p.front() == '*')
        {
            p.remove_prefix(1);
            for (usize i = 0; i <= s.size(); ++i) // within the segment
            {
                if (matchFrom(p, s.substr(i)))
                {
                    return true;
                }
                if (i < s.size() && s[i] == '/')
                {
                    return false;
                }
            }
            return false;
        }
        if (s.empty() || p.front() != s.front())
        {
            return false;
        }
        p.remove_prefix(1);
        s.remove_prefix(1);
    }
    return s.empty();
}

std::vector<std::string> strings(const Config& config, const std::string& key)
{
    std::vector<std::string> out;
    for (usize i = 0; i < config.arraySize(key); ++i)
    {
        if (auto value = config.find<std::string>(std::format("{}[{}]", key, i)))
        {
            out.push_back(*value);
        }
    }
    return out;
}
} // namespace

bool globMatch(std::string_view pattern, std::string_view path) noexcept
{
    return matchFrom(pattern, path);
}

Result<LicenceTable> parseLicences(std::string_view toml, std::string_view sourceName)
{
    auto config = Config::parse(toml, sourceName);
    if (!config)
    {
        return config.error();
    }
    const Config& c = config.value();
    if (c.get<i64>("version", 0) != 1)
    {
        return Error{std::format("{}: version must be 1", sourceName)};
    }
    LicenceTable table;
    for (const std::string& id : c.keys("licence"))
    {
        const std::string at = "licence." + id;
        LicenceKind kind{id, c.get<std::string>(at + ".name", id),
                         c.get<bool>(at + ".redistributable", false),
                         c.get<bool>(at + ".attribution", false)};
        table.kinds.emplace(id, std::move(kind));
    }
    for (usize i = 0; i < c.arraySize("source"); ++i)
    {
        const std::string at = std::format("source[{}]", i);
        LicenceSource s;
        s.name = c.get<std::string>(at + ".name", "");
        s.licences = strings(c, at + ".licences");
        s.paths = strings(c, at + ".paths");
        s.credit = c.get<std::string>(at + ".credit", "");
        s.thanks = c.get<std::string>(at + ".thanks", "");
        s.md = c.get<std::string>(at + ".md", "");
        s.optionalMd = c.get<bool>(at + ".optional_md", false);
        s.creditOrder = c.get<i64>(at + ".credit_order", 100);
        if (s.name.empty() || s.licences.empty() || s.paths.empty())
        {
            return Error{std::format("{}: {} needs name, licences and paths", sourceName, at)};
        }
        table.sources.push_back(std::move(s));
    }
    return table;
}

std::string licenceKindOf(std::string_view text)
{
    // Most specific first: "eigene Arbeit des Projektinhabers" before "eigene Arbeit".
    struct Rule
    {
        std::string_view phrase;
        std::string_view kind;
    };
    constexpr Rule kRules[] = {
        {"CMU", "CMU-MoCap"},
        {"mocap.cs.cmu.edu", "CMU-MoCap"},
        {"Datenlizenz Deutschland", "DL-DE-BY-2_0"},
        {"ODbL", "ODbL-1_0"},
        {"Open Font License", "OFL-1_1"},
        {"CC0", "CC0-1_0"},
        {"CC BY 4.0", "CC-BY-4_0"},
        {"Public Domain", "public-domain"},
        {"eigene Arbeit des Projektinhabers", "owner"},
        {"eigene Arbeit", "own"},
        {"wie das Repository", "own"},
    };
    for (const Rule& r : kRules)
    {
        if (text.find(r.phrase) != std::string_view::npos)
        {
            return std::string(r.kind);
        }
    }
    return {};
}

std::vector<std::string> markdownLicences(std::string_view md)
{
    // Tables: a header row with a "Lizenz" column, a separator, then rows; a blank line ends a table.
    std::vector<std::string> out;
    i64 column = -1;
    usize start = 0;
    while (start < md.size())
    {
        const usize end = std::min(md.find('\n', start), md.size());
        std::string_view line = md.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (!line.starts_with("|"))
        {
            column = -1;
            continue;
        }
        std::vector<std::string_view> cells;
        usize at = 1;
        while (at < line.size())
        {
            const usize next = line.find('|', at);
            if (next == std::string_view::npos)
            {
                break;
            }
            std::string_view cell = line.substr(at, next - at);
            while (!cell.empty() && cell.front() == ' ')
            {
                cell.remove_prefix(1);
            }
            while (!cell.empty() && cell.back() == ' ')
            {
                cell.remove_suffix(1);
            }
            cells.push_back(cell);
            at = next + 1;
        }
        if (column < 0)
        {
            const auto header = std::ranges::find(cells, std::string_view("Lizenz"));
            column = header != cells.end() ? static_cast<i64>(header - cells.begin()) : -1;
            continue;
        }
        if (cells.empty() || cells[0].starts_with("---"))
        {
            continue;
        }
        if (static_cast<usize>(column) < cells.size())
        {
            out.emplace_back(cells[static_cast<usize>(column)]);
        }
    }
    return out;
}

LicenceReport checkLicences(const LicenceTable& table, const std::vector<std::string>& files,
                            std::string_view licensesMd)
{
    LicenceReport report;
    // The table itself: known kinds, credits where required, the readable list mentions each source.
    for (const LicenceSource& s : table.sources)
    {
        bool needsCredit = false;
        for (const std::string& id : s.licences)
        {
            const auto kind = table.kinds.find(id);
            if (kind == table.kinds.end())
            {
                report.tableErrors.push_back(std::format("{}: unknown licence '{}'", s.name, id));
                continue;
            }
            needsCredit = needsCredit || kind->second.attribution;
        }
        if (needsCredit && s.credit.empty())
        {
            report.tableErrors.push_back(
                std::format("{}: its licence asks for a credit line - 'credit' is empty", s.name));
        }
        if (!s.optionalMd && (s.md.empty() || licensesMd.find(s.md) == std::string_view::npos))
        {
            report.tableErrors.push_back(std::format("{}: '{}' is not in LICENSES.md", s.name, s.md));
        }
    }
    // Every licence the readable list names is a kind of this table and may be shipped (CMU included).
    for (const std::string& text : markdownLicences(licensesMd))
    {
        const std::string id = licenceKindOf(text);
        const auto kind = id.empty() ? table.kinds.end() : table.kinds.find(id);
        if (kind == table.kinds.end())
        {
            report.tableErrors.push_back(
                std::format("LICENSES.md: licence \"{}\" has no kind in licenses.toml", text));
        }
        else if (!kind->second.redistributable)
        {
            report.tableErrors.push_back(std::format("LICENSES.md: licence \"{}\" may not be shipped", text));
        }
    }
    for (const std::string& file : files)
    {
        const auto source = std::ranges::find_if(
            table.sources,
            [&](const LicenceSource& s)
            {
                return std::ranges::any_of(s.paths, [&](const std::string& p) { return globMatch(p, file); });
            });
        if (source == table.sources.end())
        {
            report.uncovered.push_back(file);
            continue;
        }
        ++report.used[static_cast<usize>(source - table.sources.begin())];
        for (const std::string& id : source->licences)
        {
            const auto kind = table.kinds.find(id);
            if (kind != table.kinds.end() && !kind->second.redistributable)
            {
                report.forbidden.push_back(std::format("{}: {}", file, kind->second.name));
            }
        }
    }
    return report;
}

std::string creditsText(const LicenceTable& table, const LicenceReport& report, std::string_view versionLine)
{
    std::string out = std::format("{}\n\nMitwirkende und Quellen\n=======================\n\n", versionLine);
    out += "Pflichtangaben\n--------------\n";
    std::vector<usize> credited;
    for (const auto& [index, count] : report.used)
    {
        if (!table.sources[index].credit.empty())
        {
            credited.push_back(index);
        }
    }
    std::ranges::stable_sort(credited, {}, [&](usize i) { return table.sources[i].creditOrder; });
    for (const usize index : credited)
    {
        out += "- " + table.sources[index].credit + "\n";
    }
    out += "\nDank an\n-------\n";
    for (const auto& [index, count] : report.used)
    {
        if (!table.sources[index].thanks.empty())
        {
            out += "- " + table.sources[index].thanks + "\n";
        }
    }
    out += "\nAlle Quellen mit Lizenz und Verwendung: LICENSES/ASSETS.md. Bibliotheken: "
           "THIRD_PARTY_NOTICES.txt.\n";
    return out;
}

std::vector<std::string> assetFiles(const fs::Path& source)
{
    std::vector<std::string> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(source, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        const std::string name = fs::toUtf8(it->path().filename());
        if (name.starts_with(".") && name != ".gitignore")
        {
            if (it->is_directory(ec))
            {
                it.disable_recursion_pending(); // .venv and the like
            }
            continue;
        }
        if (it->is_regular_file(ec))
        {
            std::string relative = fs::toUtf8(std::filesystem::relative(it->path(), source, ec));
            std::ranges::replace(relative, '\\', '/');
            files.push_back(std::move(relative));
        }
    }
    std::ranges::sort(files);
    return files;
}
} // namespace g7::cook

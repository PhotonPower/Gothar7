#include <g7/core/Config.hpp>
#include <g7/runtime/Credits.hpp>

#include <algorithm>
#include <format>
#include <map>

namespace g7
{
namespace
{
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

/// `key_en` in English when it is there, else `key`.
std::string localised(const Config& config, const std::string& key, bool english)
{
    if (english)
    {
        if (auto value = config.find<std::string>(key + "_en"))
        {
            return *value;
        }
    }
    return config.get<std::string>(key, "");
}
} // namespace

Result<CreditsData> parseCredits(std::string_view licensesToml, std::string_view notices,
                                 std::string_view language)
{
    auto config = Config::parse(licensesToml, "licenses.toml");
    if (!config)
    {
        return config.error();
    }
    const Config& c = config.value();
    if (c.get<i64>("version", 0) != 1)
    {
        return Error{"licenses.toml: version must be 1"};
    }
    const bool english = language == "en";
    std::map<std::string, std::string, std::less<>> kinds;
    for (const std::string& id : c.keys("licence"))
    {
        const std::string name = localised(c, "licence." + id + ".name", english);
        kinds.emplace(id, name.empty() ? id : name);
    }
    CreditsData data;
    for (usize i = 0; i < c.arraySize("source"); ++i)
    {
        const std::string at = std::format("source[{}]", i);
        CreditSource s;
        s.name = c.get<std::string>(at + ".name", "");
        if (s.name.empty())
        {
            return Error{std::format("licenses.toml: {} has no name", at)};
        }
        for (const std::string& id : strings(c, at + ".licences"))
        {
            const auto kind = kinds.find(id);
            s.licences.push_back(kind != kinds.end() ? kind->second : id);
        }
        s.credit = localised(c, at + ".credit", english);
        s.thanks = localised(c, at + ".thanks", english);
        s.creditOrder = c.get<i64>(at + ".credit_order", 100);
        data.sources.push_back(std::move(s));
    }
    // The libraries in the game: the "- " lines before the tools' part ("Nur in den Werkzeugen").
    for (usize start = 0; start < notices.size();)
    {
        usize end = notices.find('\n', start);
        end = end == std::string_view::npos ? notices.size() : end;
        std::string_view line = notices.substr(start, end - start);
        start = end + 1;
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        if (line.starts_with("Nur in den Werkzeugen"))
        {
            break;
        }
        if (line.starts_with("- "))
        {
            data.libraries.emplace_back(line.substr(2));
        }
    }
    return data;
}

std::string creditsScreenText(const CreditsData& data, const CreditsHeadings& headings)
{
    std::string out = headings.required + "\n";
    std::vector<const CreditSource*> credited;
    for (const CreditSource& s : data.sources)
    {
        if (!s.credit.empty())
        {
            credited.push_back(&s);
        }
    }
    std::ranges::stable_sort(credited, {}, &CreditSource::creditOrder);
    for (const CreditSource* s : credited)
    {
        out += "- " + s->credit + "\n";
    }
    out += "\n" + headings.sources + "\n";
    for (const CreditSource& s : data.sources)
    {
        std::string licences;
        for (const std::string& l : s.licences)
        {
            licences += (licences.empty() ? "" : ", ") + l;
        }
        out += "- " + s.name + ": " + licences + "\n";
    }
    out += "\n" + headings.thanks + "\n";
    for (const CreditSource& s : data.sources)
    {
        if (!s.thanks.empty())
        {
            out += "- " + s.thanks + "\n";
        }
    }
    if (!data.libraries.empty())
    {
        out += "\n" + headings.libraries + "\n";
        for (const std::string& l : data.libraries)
        {
            out += "- " + l + "\n";
        }
    }
    return out;
}
} // namespace g7

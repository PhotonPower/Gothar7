#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>
#include <g7/ui/Strings.hpp>

#include <string>

namespace g7::ui
{
Result<Strings> Strings::load(std::string language, std::string_view toml, std::string_view sourceName)
{
    auto parsed = Config::parse(toml, sourceName);
    if (!parsed)
    {
        return parsed.error();
    }
    Strings strings;
    strings.m_language = std::move(language);
    for (const std::string& key : parsed.value().keys())
    {
        auto text = parsed.value().find<std::string>(key);
        if (!text)
        {
            return Error{std::string(sourceName) + ": '" + key + "' is not a string"};
        }
        strings.m_texts.emplace(key, std::move(*text));
    }
    return strings;
}

bool Strings::contains(std::string_view key) const
{
    return m_texts.contains(key);
}

std::string_view Strings::get(std::string_view key) const
{
    const auto it = m_texts.find(key);
    if (it != m_texts.end())
    {
        return it->second;
    }
    if (m_reportedMissing.emplace(key).second)
    {
        G7_LOG_WARN("ui", "text '{}' missing for language '{}'", key, m_language);
    }
    return key;
}

std::string Strings::format(std::string_view key, std::initializer_list<TextArg> args) const
{
    return substitute(get(key), args);
}

std::set<std::string> Strings::keys() const
{
    std::set<std::string> out;
    for (const auto& [key, text] : m_texts)
    {
        out.insert(key);
    }
    return out;
}

std::string substitute(std::string_view text, std::initializer_list<TextArg> args)
{
    std::string out;
    out.reserve(text.size());
    for (usize i = 0; i < text.size();)
    {
        if (text[i] == '{')
        {
            const usize close = text.find('}', i + 1);
            if (close != std::string_view::npos)
            {
                const std::string_view name = text.substr(i + 1, close - i - 1);
                const TextArg* match = nullptr;
                for (const TextArg& arg : args)
                {
                    if (arg.name == name)
                    {
                        match = &arg;
                        break;
                    }
                }
                if (match != nullptr)
                {
                    out += match->value;
                    i = close + 1;
                    continue;
                }
            }
        }
        out += text[i];
        ++i;
    }
    return out;
}

std::string formatNumber(i64 value, std::string_view language)
{
    const char separator = language == "de" ? '.' : ',';
    // Magnitude as unsigned, so the smallest i64 has no overflow.
    const u64 magnitude = value < 0 ? static_cast<u64>(-(value + 1)) + 1 : static_cast<u64>(value);
    const std::string digits = std::to_string(magnitude);
    std::string out;
    if (value < 0)
    {
        out += '-';
    }
    for (usize i = 0; i < digits.size(); ++i)
    {
        if (i > 0 && (digits.size() - i) % 3 == 0)
        {
            out += separator;
        }
        out += digits[i];
    }
    return out;
}
} // namespace g7::ui

#pragma once

// UI texts by key (M14 part A): assets/source/text/ui.<language>.toml, one flat table per language
// (UI_INVENTORY_TITLE = "Inventar"). A missing key shows the key itself and is reported once.

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace g7::ui
{
/// A value for a "{name}" placeholder.
struct TextArg
{
    std::string_view name;
    std::string value;
};

class Strings
{
public:
    Strings() = default;
    /// Parses a table of `KEY = "text"` pairs (strings only; other values are errors).
    [[nodiscard]] static Result<Strings> load(std::string language, std::string_view toml,
                                              std::string_view sourceName = "<strings>");

    [[nodiscard]] const std::string& language() const noexcept { return m_language; }
    [[nodiscard]] bool contains(std::string_view key) const;
    /// The text of `key`, or the key itself (reported once) if the table lacks it.
    [[nodiscard]] std::string_view get(std::string_view key) const;
    /// get() with "{name}" replaced by the matching argument; unknown placeholders stay as they are.
    [[nodiscard]] std::string format(std::string_view key, std::initializer_list<TextArg> args) const;
    /// Every key, sorted (tests: both languages have the same keys).
    [[nodiscard]] std::set<std::string> keys() const;

private:
    std::string m_language;
    std::map<std::string, std::string, std::less<>> m_texts;
    mutable std::set<std::string, std::less<>> m_reportedMissing;
};

/// Replaces "{name}" in `text` by the matching argument.
[[nodiscard]] std::string substitute(std::string_view text, std::initializer_list<TextArg> args);

/// An integer with the language's digit grouping: "de" 1.234.567, "en" 1,234,567 (others like "en").
[[nodiscard]] std::string formatNumber(i64 value, std::string_view language);
} // namespace g7::ui

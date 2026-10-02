#pragma once

#include <g7/core/Types.hpp>

#include <string>
#include <string_view>

/// String helpers. Case folding is ASCII-only: Gothic-style names (instances, waypoints, vobs)
/// are ASCII, and all other bytes (e.g. UTF-8 umlauts in texts) pass through unchanged.
namespace g7
{
[[nodiscard]] constexpr char toLowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
[[nodiscard]] constexpr char toUpperAscii(char c) noexcept
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] std::string toLower(std::string_view s);
[[nodiscard]] std::string toUpper(std::string_view s);

[[nodiscard]] constexpr bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (usize i = 0; i < a.size(); ++i)
    {
        if (toLowerAscii(a[i]) != toLowerAscii(b[i]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool startsWithIgnoreCase(std::string_view s, std::string_view prefix) noexcept
{
    return s.size() >= prefix.size() && equalsIgnoreCase(s.substr(0, prefix.size()), prefix);
}

/// Transparent hash/equality for case-insensitive lookup in unordered containers:
/// std::unordered_map<std::string, T, IgnoreCaseHash, IgnoreCaseEqual>.
struct IgnoreCaseHash
{
    using is_transparent = void;
    [[nodiscard]] usize operator()(std::string_view s) const noexcept;
};

struct IgnoreCaseEqual
{
    using is_transparent = void;
    [[nodiscard]] constexpr bool operator()(std::string_view a, std::string_view b) const noexcept
    {
        return equalsIgnoreCase(a, b);
    }
};
} // namespace g7

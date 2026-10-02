#pragma once

#include <g7/core/StringUtil.hpp>
#include <g7/core/Types.hpp>

#include <compare>
#include <format>
#include <functional>
#include <string_view>

/// G7_STRINGID_NAMES: keep the plain text of runtime-created ids for log output and detect
/// hash collisions. On by default in debug builds.
#ifndef G7_STRINGID_NAMES
#ifdef NDEBUG
#define G7_STRINGID_NAMES 0
#else
#define G7_STRINGID_NAMES 1
#endif
#endif

namespace g7
{
/// Hashed, case-insensitive name for fast comparisons ("WP_HC_CAMPFIRE" == "wp_hc_campfire").
/// 64-bit FNV-1a over the ASCII-lowercased bytes; 64 bits keep collisions negligible for the
/// tens of thousands of names a world has.
class StringId
{
public:
    /// Invalid id (hash 0).
    constexpr StringId() noexcept = default;
    /// Hashes the name; with G7_STRINGID_NAMES also registers it for name() and collision checks.
    explicit StringId(std::string_view name);

    [[nodiscard]] static constexpr StringId fromHash(u64 hash) noexcept
    {
        StringId id;
        id.m_hash = hash;
        return id;
    }

    [[nodiscard]] static constexpr u64 hashOf(std::string_view name) noexcept
    {
        u64 hash = kFnvOffsetBasis;
        for (const char c : name)
        {
            hash ^= static_cast<u8>(toLowerAscii(c));
            hash *= kFnvPrime;
        }
        return hash;
    }

    [[nodiscard]] constexpr u64 hash() const noexcept { return m_hash; }
    [[nodiscard]] constexpr bool valid() const noexcept { return m_hash != 0; }

    /// Plain text as first registered at runtime; empty if unknown or names are compiled out.
    [[nodiscard]] std::string_view name() const;

    constexpr auto operator<=>(const StringId&) const noexcept = default;

private:
    static constexpr u64 kFnvOffsetBasis = 0xcbf29ce484222325ull;
    static constexpr u64 kFnvPrime = 0x100000001b3ull;

    u64 m_hash = 0;
};

namespace literals
{
/// Compile-time id, usable in switch/static_assert. Not registered: name() stays empty until
/// the same name is created once at runtime.
[[nodiscard]] consteval StringId operator""_sid(const char* text, usize length) noexcept
{
    return StringId::fromHash(StringId::hashOf(std::string_view(text, length)));
}
} // namespace literals
} // namespace g7

template <>
struct std::hash<g7::StringId>
{
    [[nodiscard]] std::size_t operator()(const g7::StringId& id) const noexcept
    {
        return static_cast<std::size_t>(id.hash());
    }
};

/// Formats as the registered name, or "#<16 hex digits>" if the name is unknown.
template <>
struct std::formatter<g7::StringId> : std::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const g7::StringId& id, FormatContext& ctx) const
    {
        const std::string_view name = id.name();
        if (!name.empty())
        {
            return std::formatter<std::string_view>::format(name, ctx);
        }
        return std::format_to(ctx.out(), "#{:016x}", id.hash());
    }
};

#pragma once

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace g7
{
/// Value types a Config can hold.
template <typename T>
concept ConfigValue = std::is_same_v<T, bool> || std::is_same_v<T, i64> || std::is_same_v<T, f64> ||
                      std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<std::string>> ||
                      std::is_same_v<T, std::vector<f64>>;

/// Typed key/value configuration backed by TOML (ADR 0010).
///
/// Keys are dotted paths into nested tables: "render.view_distance", "audio.volume.music".
/// Supported value types: bool, i64, f64, std::string, std::vector<std::string>, std::vector<f64>.
/// f64 queries (also inside arrays) accept integers, so "volume = 1" reads as 1.0. Array elements are
/// addressed with an index: "object[2].mesh" (arrays of tables, `[[object]]` in TOML). The type is always
/// given explicitly (`get<i64>("render.fps_limit", 0)`), so literals cannot pick an unsupported one.
class Config
{
public:
    Config();
    ~Config();
    Config(const Config& other);
    Config& operator=(const Config& other);
    Config(Config&& other) noexcept;
    Config& operator=(Config&& other) noexcept;

    /// Parses TOML text. Errors carry source name, line, column and description.
    [[nodiscard]] static Result<Config> parse(std::string_view toml,
                                              std::string_view sourceName = "<string>");
    [[nodiscard]] static Result<Config> load(const fs::Path& path);
    /// Writes atomically, so a crash never leaves a broken config behind.
    [[nodiscard]] Result<void> save(const fs::Path& path) const;
    [[nodiscard]] std::string toToml() const;

    [[nodiscard]] bool contains(std::string_view key) const;
    /// Number of elements of the array at `key`; 0 if absent or not an array.
    [[nodiscard]] usize arraySize(std::string_view key) const;

    /// Value if present and of the requested type, otherwise nullopt.
    template <ConfigValue T>
    [[nodiscard]] std::optional<T> find(std::string_view key) const;

    /// Value if present and of the requested type, otherwise `defaultValue`.
    /// A present value of the wrong type is logged as a warning.
    template <ConfigValue T>
    [[nodiscard]] T get(std::string_view key, std::type_identity_t<T> defaultValue) const;

    /// Sets the value, creating intermediate tables and replacing whatever was there.
    template <ConfigValue T>
    void set(std::string_view key, std::type_identity_t<T> value);

    /// Direct child keys of the table at `table` (root if empty), sorted. Empty if not a table.
    [[nodiscard]] std::vector<std::string> keys(std::string_view table = {}) const;

    /// Applies `overrides` on top: its values replace ours, tables are merged recursively,
    /// keys only present here are kept. Used for default config <- user config <- command line.
    void merge(const Config& overrides);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7

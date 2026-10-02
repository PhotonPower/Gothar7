#pragma once

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::fs
{
using Path = std::filesystem::path;

/// Converts a UTF-8 string to a path. On Windows, constructing a path from std::string would
/// interpret it in the ANSI codepage, so all string paths crossing the API go through here.
[[nodiscard]] Path fromUtf8(std::string_view utf8);
/// Converts a path to UTF-8 (for logging, config files, scripts).
[[nodiscard]] std::string toUtf8(const Path& path);

/// Reads the whole file as bytes.
[[nodiscard]] Result<std::vector<u8>> readFile(const Path& path);
/// Reads the whole file as text (bytes are returned unchanged, no newline conversion).
[[nodiscard]] Result<std::string> readText(const Path& path);

/// Creates or truncates the file and writes the data.
[[nodiscard]] Result<void> writeFile(const Path& path, std::span<const u8> data);
[[nodiscard]] Result<void> writeText(const Path& path, std::string_view text);

/// Writes to a temporary file next to the target and renames it over the target, so readers
/// (and a crash mid-write) never see a half-written save or config file.
[[nodiscard]] Result<void> writeFileAtomic(const Path& path, std::span<const u8> data);
[[nodiscard]] Result<void> writeTextAtomic(const Path& path, std::string_view text);

/// Creates the directory and all missing parents. Succeeds if it already exists.
[[nodiscard]] Result<void> createDirectories(const Path& path);
[[nodiscard]] bool exists(const Path& path) noexcept;

/// Root directories of the running game. Documented subsystem singleton: set once at startup
/// (game main in M0, platform module from M1 on), read everywhere else.
struct BaseDirectories
{
    Path gameDir; ///< Installation directory (assets, scripts).
    Path userDir; ///< Writable per-user directory (saves, config).
};

void setBaseDirectories(BaseDirectories dirs);
[[nodiscard]] const BaseDirectories& baseDirectories() noexcept;

/// Resolves a UTF-8 relative path against the game directory, e.g. "assets/cooked/world.g7pak".
[[nodiscard]] Path gamePath(std::string_view relativeUtf8);
/// Resolves a UTF-8 relative path against the user directory, e.g. "saves/quick.sav".
[[nodiscard]] Path userPath(std::string_view relativeUtf8);
} // namespace g7::fs

#pragma once

// Version and build info (M17, docs/modules/release.md part A): generated at every build from CMake's project
// version and git (cmake/BuildInfo.cmake). Shown in the log, the main menu, `gothar --version`, save games and
// crash reports.

#include <string>
#include <string_view>

namespace g7
{
struct BuildInfo
{
    std::string_view version;    ///< "0.3.0" (project(VERSION) in CMakeLists.txt)
    std::string_view gitHash;    ///< 7 characters, "unknown" without git
    bool dirty = false;          ///< built with changes git knows about but not committed
    std::string_view buildType;  ///< "debug", "release", "relwithdebinfo"
    std::string_view commitDate; ///< "2026-11-07" (empty without git)
};

[[nodiscard]] const BuildInfo& buildInfo() noexcept;

/// "0.3.0 (abc1234)" - "+" after the hash for a dirty build: the main menu and save games.
[[nodiscard]] std::string versionText(const BuildInfo& info = buildInfo());
/// "Gothar 0.3.0 (abc1234, release)": the log's first line, `gothar --version`, crash reports.
[[nodiscard]] std::string buildLine(const BuildInfo& info = buildInfo());
} // namespace g7

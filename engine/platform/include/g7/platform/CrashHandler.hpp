#pragma once

// Local crash reports (M17, docs/modules/release.md part B): on a crash the game writes, into a folder of its
// own, a minidump (Windows) and a text report (build line, reason, the last log lines). Nothing is sent
// anywhere. The next start finds the new reports and says so once.

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace g7::platform
{
struct CrashSettings
{
    fs::Path directory;    ///< where the reports go (created if needed); tests: always a folder of their own
    std::string buildLine; ///< "Gothar 0.3.0 (abc1234, release)"
    bool minidump = true;  ///< Windows: also the .dmp (stack and threads, no full memory)
};

/// Catches unhandled exceptions/signals (access violations, illegal instructions, ...), uncaught C++
/// exceptions (std::terminate) and failed G7_VERIFY/G7_ASSERT, writes the report and ends the process with
/// exit code 3 - without the system's "program stopped working" dialog. One handler per process; installing
/// again replaces the settings.
[[nodiscard]] Result<void> installCrashHandler(CrashSettings settings);
void uninstallCrashHandler() noexcept;

/// Writes a report now (without a minidump) for `reason` - what the handler does; also usable by hand.
/// Returns the text file.
[[nodiscard]] Result<fs::Path> writeCrashReport(std::string_view reason);

/// The reports (text files) in `directory` that are newer than the last markReportsSeen; newest first.
[[nodiscard]] std::vector<fs::Path> unseenCrashReports(const fs::Path& directory);
/// Remembers that the reports there have been pointed out (a marker file in the folder).
void markReportsSeen(const fs::Path& directory);
} // namespace g7::platform

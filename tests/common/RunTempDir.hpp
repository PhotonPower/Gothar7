#pragma once

// A temporary folder of the test run's own: tests that write files put them below it instead of straight into
// %TEMP%, so parallel runs (other worktrees, ctest -j) never share a file. Removed when the run ends.

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace g7::test
{
/// The run's folder below the system's temporary directory (created on first use).
[[nodiscard]] inline const std::filesystem::path& runTempDir()
{
    struct Folder
    {
        std::filesystem::path path;
        Folder()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() /
                   ("g7_run_" + std::to_string(stamp) + "_" + std::to_string(std::random_device{}()));
            std::error_code ignored;
            std::filesystem::create_directories(path, ignored);
        }
        ~Folder()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
        Folder(const Folder&) = delete;
        Folder& operator=(const Folder&) = delete;
    };
    static const Folder folder;
    return folder.path;
}
} // namespace g7::test

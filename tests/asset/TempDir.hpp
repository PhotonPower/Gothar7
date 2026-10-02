#pragma once

#include <g7/core/FileSystem.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace g7::test
{
/// Fresh directory per test case, removed on scope exit.
class TempDir
{
public:
    TempDir()
    {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("g7_asset_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(m_path);
    }
    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const fs::Path& path() const { return m_path; }

private:
    fs::Path m_path;
};
} // namespace g7::test

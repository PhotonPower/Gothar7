// Crash reports (M17, release.md part B): a probe process crashes on purpose (access violation, uncaught
// exception, failed G7_VERIFY) and leaves its report in a folder of the test run - never in the user's
// folder; the test runner itself never crashes. Unseen reports are found once.

#include "RunTempDir.hpp"

#include <g7/core/FileSystem.hpp>
#include <g7/platform/CrashHandler.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace g7;

namespace
{
/// Runs the probe; its exit code.
int runProbe(const std::string& kind, const std::filesystem::path& folder)
{
    const std::string command = std::format("\"{}\" {} \"{}\"", G7_CRASH_PROBE, kind, fs::toUtf8(folder));
#ifdef _WIN32
    return std::system(("\"" + command + "\"").c_str()); // cmd /c strips the outer quotes
#else
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

std::vector<std::filesystem::path> files(const std::filesystem::path& folder, std::string_view extension)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec))
    {
        if (e.path().extension() == extension)
        {
            out.push_back(e.path());
        }
    }
    return out;
}
} // namespace

TEST_CASE("Crash handler: a probe crashing in three ways leaves its reports in the given folder")
{
    for (const std::string kind : {"segv", "throw", "verify"})
    {
        CAPTURE(kind);
        const std::filesystem::path folder = test::runTempDir() / ("g7_crash_" + kind);
        CHECK(runProbe(kind, folder) == 3);
        const auto reports = files(folder, ".txt");
        REQUIRE(reports.size() == 1);
        const std::string text = fs::readText(reports[0]).value();
        CHECK(text.starts_with("Gothar 9.9.9 (probe12, test)\n"));
        CHECK(text.find("about to crash: " + kind) != std::string::npos); // the last log lines
        CHECK(fs::toUtf8(reports[0].filename()).starts_with("gothar_9.9.9_probe12_"));
        if (kind == "throw")
        {
            CHECK(text.find("uncaught exception") !=
                  std::string::npos); // with what() where the platform tells
        }
        if (kind == "verify")
        {
            CHECK(text.find("assert: probe verify") != std::string::npos);
        }
#ifdef _WIN32
        const auto dumps = files(folder, ".dmp");
        REQUIRE(dumps.size() == 1);
        CHECK(std::filesystem::file_size(dumps[0]) > 0);
#endif
    }
    // No crash, no report.
    const std::filesystem::path calm = test::runTempDir() / "g7_crash_none";
    CHECK(runProbe("none", calm) == 0);
    CHECK(files(calm, ".txt").empty());
}

TEST_CASE("Crash handler: unseen reports are found once, newest first")
{
    const std::filesystem::path folder = test::runTempDir() / "g7_crash_seen";
    std::filesystem::create_directories(folder);
    CHECK(platform::unseenCrashReports(folder).empty());
    REQUIRE(fs::writeText(folder / "a.txt", "a").ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(fs::writeText(folder / "b.txt", "b").ok());
    const auto unseen = platform::unseenCrashReports(folder);
    REQUIRE(unseen.size() == 2);
    CHECK(unseen[0].filename() == "b.txt");
    platform::markReportsSeen(folder);
    CHECK(platform::unseenCrashReports(folder).empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(fs::writeText(folder / "c.txt", "c").ok());
    REQUIRE(platform::unseenCrashReports(folder).size() == 1);
    CHECK(platform::unseenCrashReports(test::runTempDir() / "g7_no_such_folder").empty());
}

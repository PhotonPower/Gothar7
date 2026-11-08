// Local crash reports (M17, release.md part B). Best effort by design: once the process is crashing, writing
// a file and a few log lines is all that is attempted; nothing leaves the machine.

#include <g7/core/Assert.hpp>
#include <g7/core/Log.hpp>
#include <g7/platform/CrashHandler.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first
#include <dbghelp.h>
#include <process.h>
#else
#include <execinfo.h>
#include <unistd.h>

#include <csignal>
#endif

namespace g7::platform
{
namespace
{
constexpr int kCrashExitCode = 3;
constexpr std::string_view kSeenMarker = "seen.marker";

std::mutex g_mutex; ///< one report at a time (two threads crashing together)
CrashSettings g_settings;
bool g_installed = false;
std::terminate_handler g_previousTerminate = nullptr;

int processId() noexcept
{
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

/// "gothar_0.3.0_abc1234_20261107-143012_4711" from the build line and the time.
std::string reportStem()
{
    std::string version = "unknown";
    std::string hash = "unknown";
    const std::string& line = g_settings.buildLine; // "Gothar 0.3.0 (abc1234, release)"
    if (const usize space = line.find(' '); space != std::string::npos)
    {
        const usize open = line.find(" (", space + 1);
        version = line.substr(space + 1, open == std::string::npos ? std::string::npos : open - space - 1);
        if (open != std::string::npos)
        {
            const usize end = line.find_first_of(",)", open + 2);
            hash = line.substr(open + 2, end == std::string::npos ? std::string::npos : end - open - 2);
        }
    }
    const auto clean = [](std::string s)
    {
        std::ranges::replace_if(
            s, [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '.'; }, '_');
        return s;
    };
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32] = {};
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
    return std::format("gothar_{}_{}_{}_{}", clean(version), clean(hash), stamp, processId());
}

/// The text report; `extra` (the backtrace on POSIX) is written by the caller into the open file.
std::FILE* openReport(const fs::Path& file, std::string_view reason)
{
    std::FILE* out = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&out, file.c_str(), L"wb") != 0)
    {
        return nullptr;
    }
#else
    out = std::fopen(file.c_str(), "wb");
    if (out == nullptr)
    {
        return nullptr;
    }
#endif
    std::fprintf(out, "%s\nreason: %.*s\npid: %d\n\n--- log (last lines) ---\n", g_settings.buildLine.c_str(),
                 static_cast<int>(reason.size()), reason.data(), processId());
    for (const std::string& line : log::recentLines(false))
    {
        std::fprintf(out, "%s\n", line.c_str());
    }
    std::fflush(out);
    return out;
}

/// Writes the text report (and on Windows the minidump for `exception`); the stem of both files.
fs::Path writeReport(std::string_view reason, [[maybe_unused]] void* exception)
{
    std::scoped_lock lock(g_mutex);
    std::error_code ec;
    std::filesystem::create_directories(g_settings.directory, ec);
    const std::string stem = reportStem();
    const fs::Path text = g_settings.directory / fs::fromUtf8(stem + ".txt");
    std::FILE* out = openReport(text, reason);
#ifdef _WIN32
    if (g_settings.minidump)
    {
        const fs::Path dump = g_settings.directory / fs::fromUtf8(stem + ".dmp");
        const HANDLE file = CreateFileW(dump.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION info{};
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(exception);
            info.ClientPointers = FALSE;
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo);
            const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                              exception != nullptr ? &info : nullptr, nullptr, nullptr);
            CloseHandle(file);
            if (out != nullptr)
            {
                std::fprintf(out, "\nminidump: %s\n", ok ? fs::toUtf8(dump.filename()).c_str() : "failed");
            }
        }
    }
#else
    if (out != nullptr)
    {
        void* frames[64];
        const int count = backtrace(frames, 64);
        std::fprintf(out, "\n--- backtrace ---\n");
        std::fflush(out);
        backtrace_symbols_fd(frames, count, fileno(out));
    }
#endif
    if (out != nullptr)
    {
        std::fclose(out);
    }
    return text;
}

[[noreturn]] void endProcess() noexcept
{
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), kCrashExitCode);
#endif
    std::_Exit(kCrashExitCode);
}

void onFatal(std::string_view what) // failed G7_VERIFY/G7_ASSERT
{
    (void)writeReport(what, nullptr);
    endProcess();
}

void onTerminate()
{
    std::string reason = "std::terminate (uncaught exception)";
    if (const std::exception_ptr current = std::current_exception())
    {
        try
        {
            std::rethrow_exception(current);
        }
        catch (const std::exception& e)
        {
            reason = std::format("uncaught exception: {}", e.what());
        }
        catch (...)
        {
            reason = "uncaught exception of unknown type";
        }
    }
    (void)writeReport(reason, nullptr);
    endProcess();
}

#ifdef _WIN32
LONG WINAPI onUnhandled(EXCEPTION_POINTERS* info)
{
    const DWORD code =
        info != nullptr && info->ExceptionRecord != nullptr ? info->ExceptionRecord->ExceptionCode : 0;
    const void* address = info != nullptr && info->ExceptionRecord != nullptr
                              ? info->ExceptionRecord->ExceptionAddress
                              : nullptr;
    // 0xE06D7363: a C++ exception nobody caught (MSVC hands it here, not to std::terminate).
    constexpr DWORD kCppException = 0xE06D7363;
    const std::string reason =
        code == kCppException
            ? std::format("uncaught exception (C++ exception 0x{:08X} at {})", static_cast<unsigned>(code),
                          address)
            : std::format("unhandled exception 0x{:08X} at {}", static_cast<unsigned>(code), address);
    (void)writeReport(reason, info);
    endProcess();
}
#else
void onSignal(int signal, siginfo_t* info, void*)
{
    (void)writeReport(std::format("signal {} ({}) at {}", signal, strsignal(signal),
                                  info != nullptr ? info->si_addr : nullptr),
                      nullptr);
    endProcess();
}
#endif
} // namespace

Result<void> installCrashHandler(CrashSettings settings)
{
    if (settings.directory.empty())
    {
        return Error{"crash handler: no directory"};
    }
    {
        std::scoped_lock lock(g_mutex);
        g_settings = std::move(settings);
    }
    if (g_installed)
    {
        return {};
    }
    g_installed = true;
    setFatalHook(&onFatal);
    g_previousTerminate = std::set_terminate(&onTerminate);
#ifdef _WIN32
    // No "program stopped working" dialog and no abort message box: the report is ours.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetUnhandledExceptionFilter(&onUnhandled);
#else
    struct sigaction action{};
    action.sa_sigaction = &onSignal;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
    {
        sigaction(signal, &action, nullptr);
    }
#endif
    return {};
}

void uninstallCrashHandler() noexcept
{
    if (!g_installed)
    {
        return;
    }
    g_installed = false;
    setFatalHook(nullptr);
    std::set_terminate(g_previousTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(nullptr);
#else
    for (const int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
    {
        std::signal(signal, SIG_DFL);
    }
#endif
}

Result<fs::Path> writeCrashReport(std::string_view reason)
{
    if (g_settings.directory.empty())
    {
        return Error{"crash handler: not installed"};
    }
    return writeReport(reason, nullptr);
}

std::vector<fs::Path> unseenCrashReports(const fs::Path& directory)
{
    std::vector<std::pair<std::filesystem::file_time_type, fs::Path>> found;
    std::error_code ec;
    const fs::Path marker = directory / fs::fromUtf8(std::string(kSeenMarker));
    const auto seen = std::filesystem::exists(marker, ec) ? std::filesystem::last_write_time(marker, ec)
                                                          : std::filesystem::file_time_type::min();
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
    {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".txt")
        {
            continue;
        }
        const auto time = entry.last_write_time(ec);
        if (!ec && time > seen)
        {
            found.emplace_back(time, entry.path());
        }
    }
    std::ranges::sort(found, std::greater<>{}, &std::pair<std::filesystem::file_time_type, fs::Path>::first);
    std::vector<fs::Path> out;
    for (auto& [time, path] : found)
    {
        out.push_back(std::move(path));
    }
    return out;
}

void markReportsSeen(const fs::Path& directory)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec))
    {
        return;
    }
    const fs::Path marker = directory / fs::fromUtf8(std::string(kSeenMarker));
    std::ofstream(marker, std::ios::binary).flush(); // the marker exists, its time is the "seen" time
    std::filesystem::last_write_time(marker, std::filesystem::file_time_type::clock::now(), ec);
}
} // namespace g7::platform

#include <g7/core/Log.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace g7::log
{
namespace
{
std::atomic<Level> g_minLevel{Level::Info};
std::mutex g_writeMutex;
constexpr std::size_t kRecentLines = 200; // kept for crash reports (release.md part B)
std::deque<std::string> g_recent;         // guarded by g_writeMutex

constexpr std::string_view levelName(Level level) noexcept
{
    switch (level)
    {
    case Level::Trace:
        return "TRACE";
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO ";
    case Level::Warn:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    case Level::Fatal:
        return "FATAL";
    }
    return "?????";
}
} // namespace

void setMinLevel(Level level) noexcept
{
    g_minLevel.store(level, std::memory_order_relaxed);
}

Level minLevel() noexcept
{
    return g_minLevel.load(std::memory_order_relaxed);
}

void write(Level level, std::string_view channel, std::string_view message)
{
    if (level < minLevel())
    {
        return;
    }
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

    const std::string line = std::format("[{:>8}ms] {} [{}] {}\n", ms, levelName(level), channel, message);

    std::scoped_lock lock(g_writeMutex);
    std::FILE* out = level >= Level::Warn ? stderr : stdout;
    std::fwrite(line.data(), 1, line.size(), out);
    std::fflush(out);
    g_recent.push_back(line.substr(0, line.size() - 1));
    if (g_recent.size() > kRecentLines)
    {
        g_recent.pop_front();
    }
}

std::vector<std::string> recentLines(bool wait)
{
    std::unique_lock lock(g_writeMutex, std::defer_lock);
    if (wait)
    {
        lock.lock();
    }
    else if (!lock.try_lock())
    {
        return {"(log busy while crashing)"}; // a crash inside write(): do not wait for ourselves
    }
    return {g_recent.begin(), g_recent.end()};
}
} // namespace g7::log

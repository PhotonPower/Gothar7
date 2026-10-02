#pragma once

#include <format>
#include <string_view>

namespace g7::log
{
enum class Level
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

/// Sets the minimum level that is emitted. Thread-safe.
void setMinLevel(Level level) noexcept;
[[nodiscard]] Level minLevel() noexcept;

/// Writes an already formatted message. Thread-safe.
void write(Level level, std::string_view channel, std::string_view message);

template <typename... Args>
void print(Level level, std::string_view channel, std::format_string<Args...> fmt, Args&&... args)
{
    if (level < minLevel())
    {
        return;
    }
    write(level, channel, std::format(fmt, std::forward<Args>(args)...));
}
} // namespace g7::log

// Channel-based logging macros. Usage: G7_LOG_INFO("render", "Loaded {} meshes", count);
#define G7_LOG_TRACE(ch, ...) ::g7::log::print(::g7::log::Level::Trace, ch, __VA_ARGS__)
#define G7_LOG_DEBUG(ch, ...) ::g7::log::print(::g7::log::Level::Debug, ch, __VA_ARGS__)
#define G7_LOG_INFO(ch, ...) ::g7::log::print(::g7::log::Level::Info, ch, __VA_ARGS__)
#define G7_LOG_WARN(ch, ...) ::g7::log::print(::g7::log::Level::Warn, ch, __VA_ARGS__)
#define G7_LOG_ERROR(ch, ...) ::g7::log::print(::g7::log::Level::Error, ch, __VA_ARGS__)
#define G7_LOG_FATAL(ch, ...) ::g7::log::print(::g7::log::Level::Fatal, ch, __VA_ARGS__)

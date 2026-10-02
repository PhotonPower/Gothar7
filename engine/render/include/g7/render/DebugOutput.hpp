#pragma once

#include <g7/core/Log.hpp>
#include <g7/core/Types.hpp>

#include <unordered_map>

namespace g7::render
{
/// GL debug message severity (GL_DEBUG_SEVERITY_*), independent of GL headers.
enum class DebugSeverity : u8
{
    Notification,
    Low,
    Medium,
    High,
};

/// Kind of GL debug message as far as logging is concerned.
enum class DebugKind : u8
{
    Error,       ///< GL_DEBUG_TYPE_ERROR
    Performance, ///< GL_DEBUG_TYPE_PERFORMANCE (e.g. redundant state changes; very chatty on some drivers)
    Other,
};

/// Log level for a GL debug message: errors and high severity are errors; performance hints
/// below high are debug-only; other medium/low messages are warnings; notifications are
/// debug-only (drivers emit them constantly).
[[nodiscard]] log::Level logLevelFor(DebugSeverity severity, DebugKind kind) noexcept;

/// Limits repeated GL debug messages: the same id is logged at most kMaxPerId times, then
/// suppressed with one final notice, so a per-frame error cannot flood the log.
class DebugMessageFilter
{
public:
    static constexpr u32 kMaxPerId = 5;

    enum class Verdict : u8
    {
        Log,
        LogLastTime, ///< Log and announce that further messages with this id are suppressed.
        Suppress,
    };

    [[nodiscard]] Verdict check(u32 id);

private:
    std::unordered_map<u32, u32> m_counts;
};
} // namespace g7::render

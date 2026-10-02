#include <g7/render/DebugOutput.hpp>

namespace g7::render
{
log::Level logLevelFor(DebugSeverity severity, DebugKind kind) noexcept
{
    if (kind == DebugKind::Error || severity == DebugSeverity::High)
    {
        return log::Level::Error;
    }
    if (kind == DebugKind::Performance)
    {
        return log::Level::Debug;
    }
    switch (severity)
    {
    case DebugSeverity::Medium:
    case DebugSeverity::Low:
        return log::Level::Warn;
    default:
        return log::Level::Debug;
    }
}

DebugMessageFilter::Verdict DebugMessageFilter::check(u32 id)
{
    const u32 count = ++m_counts[id];
    if (count < kMaxPerId)
    {
        return Verdict::Log;
    }
    return count == kMaxPerId ? Verdict::LogLastTime : Verdict::Suppress;
}
} // namespace g7::render

#include <g7/render/DebugOutput.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::render;

TEST_CASE("GL debug messages map to log levels")
{
    CHECK(logLevelFor(DebugSeverity::Low, DebugKind::Error) == log::Level::Error);
    CHECK(logLevelFor(DebugSeverity::High, DebugKind::Other) == log::Level::Error);
    CHECK(logLevelFor(DebugSeverity::High, DebugKind::Performance) == log::Level::Error);
    CHECK(logLevelFor(DebugSeverity::Medium, DebugKind::Other) == log::Level::Warn);
    CHECK(logLevelFor(DebugSeverity::Low, DebugKind::Other) == log::Level::Warn);
    CHECK(logLevelFor(DebugSeverity::Medium, DebugKind::Performance) == log::Level::Debug);
    CHECK(logLevelFor(DebugSeverity::Notification, DebugKind::Other) == log::Level::Debug);
}

TEST_CASE("Repeated GL debug messages are throttled per id")
{
    DebugMessageFilter filter;
    for (u32 i = 1; i < DebugMessageFilter::kMaxPerId; ++i)
    {
        CHECK(filter.check(42) == DebugMessageFilter::Verdict::Log);
    }
    CHECK(filter.check(42) == DebugMessageFilter::Verdict::LogLastTime);
    CHECK(filter.check(42) == DebugMessageFilter::Verdict::Suppress);
    CHECK(filter.check(42) == DebugMessageFilter::Verdict::Suppress);
    CHECK(filter.check(7) == DebugMessageFilter::Verdict::Log); // other ids unaffected
}

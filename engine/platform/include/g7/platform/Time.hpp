#pragma once

#include <g7/core/Types.hpp>

namespace g7::platform
{
/// Monotonic time in seconds since an unspecified start point.
[[nodiscard]] f64 nowSeconds() noexcept;

/// Sleeps with sub-millisecond precision (plain OS sleeps are 1–15 ms coarse on Windows).
/// Non-positive durations return immediately.
void sleepPrecise(f64 seconds) noexcept;
} // namespace g7::platform

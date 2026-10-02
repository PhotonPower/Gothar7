#pragma once

#include <g7/core/Types.hpp>

#include <string>
#include <vector>

namespace g7
{
/// Summary of recorded frame times (benchmark, `--benchmark`).
struct FrameTimeSummary
{
    usize frames = 0;
    f64 averageMs = 0.0;
    f64 p95Ms = 0.0; ///< 95 % of frames were at least this fast
    f64 p99Ms = 0.0;
    f64 worstMs = 0.0;
    [[nodiscard]] f64 averageFps() const noexcept { return averageMs > 0.0 ? 1000.0 / averageMs : 0.0; }
    /// "123 frames: 4.10 ms avg (244 fps), p95 5.00 ms, p99 6.20 ms, worst 9.80 ms"
    [[nodiscard]] std::string toString() const;
};

class FrameTimes
{
public:
    void add(f64 seconds) { m_ms.push_back(seconds * 1000.0); }
    void clear() noexcept { m_ms.clear(); }
    [[nodiscard]] usize size() const noexcept { return m_ms.size(); }
    /// Percentiles by nearest rank.
    [[nodiscard]] FrameTimeSummary summary() const;

private:
    std::vector<f64> m_ms;
};
} // namespace g7

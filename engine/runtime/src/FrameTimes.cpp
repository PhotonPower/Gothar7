#include <g7/runtime/FrameTimes.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace g7
{
std::string FrameTimeSummary::toString() const
{
    return std::format("{} frames: {:.2f} ms avg ({:.0f} fps), p95 {:.2f} ms, p99 {:.2f} ms, worst {:.2f} ms",
                       frames, averageMs, averageFps(), p95Ms, p99Ms, worstMs);
}

FrameTimeSummary FrameTimes::summary() const
{
    FrameTimeSummary s;
    s.frames = m_ms.size();
    if (m_ms.empty())
    {
        return s;
    }
    std::vector<f64> sorted = m_ms;
    std::sort(sorted.begin(), sorted.end());
    const auto rank = [&](f64 percentile)
    {
        // Nearest rank: the smallest value with at least `percentile` of the frames at or below it.
        const auto n = static_cast<f64>(sorted.size());
        const auto index = static_cast<usize>(std::max(std::ceil(percentile * n), 1.0)) - 1;
        return sorted[std::min(index, sorted.size() - 1)];
    };
    s.averageMs = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<f64>(sorted.size());
    s.p95Ms = rank(0.95);
    s.p99Ms = rank(0.99);
    s.worstMs = sorted.back();
    return s;
}
} // namespace g7

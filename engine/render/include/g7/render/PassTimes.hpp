#pragma once

// GPU time per render pass (M17, render.md "Plan M17"): timer queries around the passes of a frame, read a
// few frames later and smoothed; the engine panel and the benchmark show them against the budgets.

#include <g7/core/Types.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace g7::render
{
class Device;

struct PassTime
{
    std::string name;
    f32 milliseconds = 0.0f; ///< smoothed over the last frames
};

class PassTimes
{
public:
    /// Measures the GPU work between begin and end under `name` (passes must not nest).
    void begin(Device& device, std::string_view name);
    void end(Device& device);
    /// Reads the results that are ready (call once a frame, before the first begin).
    void collect(Device& device);
    [[nodiscard]] const std::vector<PassTime>& times() const noexcept { return m_times; }
    /// The smoothed time of a pass, 0 if it was never measured.
    [[nodiscard]] f32 milliseconds(std::string_view name) const noexcept;

private:
    static constexpr usize kRing = 4; ///< queries in flight per pass
    struct Pass
    {
        std::string name;
        std::array<rhi::TimerQuery, kRing> queries;
        std::array<bool, kRing> issued{};
        usize next = 0;
        bool created = false;
    };
    std::vector<Pass> m_passes;
    std::vector<PassTime> m_times;
    Pass* m_open = nullptr;
};

/// Exponential smoothing of a pass time: new = old + (sample - old) * 0.1 (the first sample is taken as is).
[[nodiscard]] f32 smoothPassTime(f32 previous, f32 sample, bool first) noexcept;
} // namespace g7::render

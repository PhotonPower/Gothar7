#pragma once

#include <g7/core/Types.hpp>

#include <chrono>

namespace g7
{
/// Monotonic stopwatch for frame timing and profiling.
class Stopwatch
{
public:
    Stopwatch() noexcept : m_start(Clock::now()) {}

    void reset() noexcept { m_start = Clock::now(); }
    [[nodiscard]] f64 elapsedSeconds() const noexcept
    {
        return std::chrono::duration<f64>(Clock::now() - m_start).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_start;
};

/// Fixed-timestep accumulator for the simulation loop (see docs/02-architecture.md, "Hauptschleife").
class FixedStep
{
public:
    explicit FixedStep(f64 stepSeconds, u32 maxStepsPerFrame = 5) noexcept
        : m_step(stepSeconds), m_maxSteps(maxStepsPerFrame)
    {
    }

    /// Adds real frame time; returns how many simulation steps to run this frame.
    [[nodiscard]] u32 advance(f64 frameSeconds) noexcept;

    /// Interpolation factor [0,1) between the last two simulation states for rendering.
    [[nodiscard]] f64 alpha() const noexcept { return m_accumulator / m_step; }
    [[nodiscard]] f64 step() const noexcept { return m_step; }

private:
    f64 m_step;
    f64 m_accumulator = 0.0;
    u32 m_maxSteps;
};
} // namespace g7

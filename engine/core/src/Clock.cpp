#include <g7/core/Clock.hpp>

namespace g7
{
u32 FixedStep::advance(f64 frameSeconds) noexcept
{
    m_accumulator += frameSeconds > 0.0 ? frameSeconds : 0.0;
    u32 steps = 0;
    while (m_accumulator >= m_step && steps < m_maxSteps)
    {
        m_accumulator -= m_step;
        ++steps;
    }
    // Spiral-of-death protection: drop time we could not simulate.
    if (steps == m_maxSteps && m_accumulator >= m_step)
    {
        m_accumulator = 0.0;
    }
    return steps;
}
} // namespace g7

namespace g7
{
void FramePacer::frameStarted(f64 now) noexcept
{
    if (m_interval <= 0.0)
    {
        return;
    }
    if (!m_started || now - m_nextFrame > m_interval)
    {
        m_nextFrame = now + m_interval; // first frame or hitch: restart the cadence
        m_started = true;
    }
    else
    {
        m_nextFrame += m_interval;
    }
}

f64 FramePacer::secondsUntilNextFrame(f64 now) const noexcept
{
    if (m_interval <= 0.0 || !m_started)
    {
        return 0.0;
    }
    return m_nextFrame > now ? m_nextFrame - now : 0.0;
}
} // namespace g7

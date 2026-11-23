#include <g7/gameplay/Telekinesis.hpp>

#include <algorithm>
#include <cmath>

namespace g7::gameplay
{
std::optional<u64> pickTelekinesisTarget(std::span<const TelekinesisCandidate> candidates, const Vec3& eye,
                                         const Vec3& aim, const TelekinesisSettings& settings,
                                         const std::function<bool(const TelekinesisCandidate&)>& visible)
{
    const Vec3 forward = glm::normalize(aim);
    const f32 minCos = std::cos(glm::radians(settings.aimAngle));
    std::optional<u64> best;
    f32 bestScore = 1e30f;
    for (const TelekinesisCandidate& c : candidates)
    {
        const Vec3 to = c.point - eye;
        const f32 distance = glm::length(to);
        if (!c.allowed || c.size > settings.maxSize || distance > settings.range || distance < 1e-3f)
        {
            continue;
        }
        const f32 cosine = glm::dot(to / distance, forward);
        // Nearest to the aim; of two in nearly the same line the nearer one (a degree per 4 m).
        const f32 score = glm::degrees(std::acos(std::clamp(cosine, -1.0f, 1.0f))) + 0.25f * distance;
        if (cosine < minCos || score >= bestScore || (visible && !visible(c)))
        {
            continue;
        }
        best = c.id;
        bestScore = score;
    }
    return best;
}

i32 telekinesisManaDue(f32& carry, f32 perSecond, f32 seconds) noexcept
{
    carry += std::max(perSecond, 0.0f) * std::max(seconds, 0.0f);
    const f32 whole = std::floor(carry);
    carry -= whole;
    return static_cast<i32>(whole);
}

TelekinesisFlight::TelekinesisFlight(const Vec3& start, const TelekinesisSettings& settings) noexcept
    : m_settings(settings), m_position(start), m_liftTop(start.y + settings.lift)
{
}

TelekinesisFlight::Phase TelekinesisFlight::carry(f32 seconds, const Vec3& hand,
                                                  const TelekinesisSweep& sweep)
{
    if (m_phase != Phase::Lift && m_phase != Phase::Float)
    {
        return m_phase;
    }
    m_spin += m_settings.spin * seconds;
    Vec3 step(0.0f);
    if (m_phase == Phase::Lift)
    {
        const f32 rise = std::min(m_settings.liftSpeed * seconds, m_liftTop - m_position.y);
        step = Vec3(0.0f, std::max(rise, 0.0f), 0.0f);
        if (m_position.y + step.y >= m_liftTop - 1e-4f)
        {
            m_phase = Phase::Float;
        }
    }
    else
    {
        const Vec3 to = hand - m_position;
        const f32 distance = glm::length(to);
        if (distance <= m_settings.arrive)
        {
            m_phase = Phase::Arrived;
            return m_phase;
        }
        step = to / distance * std::min(m_settings.speed * seconds, distance);
    }
    const f32 length = glm::length(step);
    const Vec3 wanted = step;
    if (length > 1e-6f && sweep)
    {
        if (const auto wall = sweep(m_position, step / length, length))
        {
            step *= std::clamp(*wall / length, 0.0f, 1.0f); // stops at the wall, floats there
            if (m_phase == Phase::Lift)
            {
                m_phase = Phase::Float; // no room to rise further
            }
        }
    }
    m_position += step;
    // Barely moving although it should: blocked (it falls after blockedDrop seconds).
    const f32 wantedLength = glm::length(wanted);
    if (wantedLength > 1e-6f && glm::length(step) < m_settings.blockedShare * wantedLength)
    {
        m_blocked += seconds;
    }
    else
    {
        m_blocked = 0.0f;
    }
    if (m_phase == Phase::Float && glm::length(hand - m_position) <= m_settings.arrive)
    {
        m_phase = Phase::Arrived;
    }
    return m_phase;
}

void TelekinesisFlight::release(f32 groundY) noexcept
{
    if (m_phase == Phase::Arrived || m_phase == Phase::Landed)
    {
        return;
    }
    m_groundY = std::min(groundY, m_position.y);
    m_fallSpeed = 0.0f;
    m_phase = Phase::Falling;
}

TelekinesisFlight::Phase TelekinesisFlight::fall(f32 seconds) noexcept
{
    if (m_phase != Phase::Falling)
    {
        return m_phase;
    }
    m_fallSpeed += m_settings.gravity * seconds;
    m_position.y -= m_fallSpeed * seconds;
    if (m_position.y <= m_groundY)
    {
        land();
    }
    return m_phase;
}

void TelekinesisFlight::land() noexcept
{
    if (m_phase != Phase::Falling)
    {
        return;
    }
    m_position.y = m_groundY;
    m_phase = Phase::Landed;
}

bool TelekinesisFlight::outOfReach(const Vec3& hand) const noexcept
{
    return glm::length(hand - m_position) > m_settings.range * m_settings.leash;
}
} // namespace g7::gameplay

#include <g7/animation/Face.hpp>

#include <algorithm>
#include <cmath>

namespace g7::animation
{
namespace
{
constexpr std::array<std::string_view, kFaceMorphCount> kNames = {
    "vis_aa",  "vis_ee",  "vis_ih",     "vis_oh",        "vis_ou",    "vis_mbp",   "vis_fv",     "vis_l",
    "blink_l", "blink_r", "expr_angry", "expr_friendly", "expr_fear", "expr_pain", "expr_sleep",
};
constexpr std::array<std::string_view, 5> kExpressions = {"angry", "friendly", "fear", "pain", "sleep"};
constexpr usize kFirstExpression = static_cast<usize>(FaceMorph::ExprAngry);
constexpr usize kVisemes = static_cast<usize>(FaceMorph::BlinkL); // vis_* come first

f32 approach(f32 value, f32 target, f32 step)
{
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}
} // namespace

std::string_view faceMorphName(FaceMorph morph) noexcept
{
    const auto index = static_cast<usize>(morph);
    return index < kNames.size() ? kNames[index] : std::string_view();
}

FaceAnimator::FaceAnimator(u32 seed, const FaceSettings& settings)
    : m_settings(settings), m_state(seed == 0 ? 1u : seed)
{
    m_blinkWait =
        m_settings.blinkMinSeconds + random01() * (m_settings.blinkMaxSeconds - m_settings.blinkMinSeconds);
}

f32 FaceAnimator::random01() noexcept
{
    // xorshift32: deterministic per figure, no global state.
    m_state ^= m_state << 13;
    m_state ^= m_state >> 17;
    m_state ^= m_state << 5;
    return static_cast<f32>(m_state >> 8) / static_cast<f32>(1u << 24);
}

bool FaceAnimator::setExpression(std::string_view name, f32 weight)
{
    i32 index = -1;
    if (!name.empty())
    {
        const auto found = std::find(kExpressions.begin(), kExpressions.end(), name);
        if (found == kExpressions.end())
        {
            return false;
        }
        index = static_cast<i32>(found - kExpressions.begin());
    }
    m_expressionTarget.fill(0.0f);
    if (index >= 0 && weight > 0.0f)
    {
        m_expressionTarget[static_cast<usize>(index)] = std::clamp(weight, 0.0f, 1.0f);
        m_expression = index;
    }
    else
    {
        m_expression = -1;
    }
    return true;
}

std::string_view FaceAnimator::expression() const noexcept
{
    return m_expression < 0 ? std::string_view() : kExpressions[static_cast<usize>(m_expression)];
}

void FaceAnimator::update(f32 seconds)
{
    const FaceSettings& s = m_settings;
    // Expressions fade towards their targets.
    const f32 fade = s.expressionSeconds > 0.0f ? seconds / s.expressionSeconds : 1.0f;
    for (usize i = 0; i < m_expressionTarget.size(); ++i)
    {
        f32& w = m_weights[kFirstExpression + i];
        w = approach(w, m_expressionTarget[i], fade);
    }

    // Blinking: a triangle closing and opening; asleep the eyes stay shut (expr_sleep closes them).
    const bool asleep = weight(FaceMorph::ExprSleep) > 0.5f;
    f32 blink = 0.0f;
    if (m_blinkTime >= 0.0f)
    {
        m_blinkTime += seconds;
        const f32 half = std::max(s.blinkSeconds * 0.5f, 1e-3f);
        blink = m_blinkTime < half ? m_blinkTime / half : std::max(0.0f, 2.0f - m_blinkTime / half);
        if (m_blinkTime >= 2.0f * half)
        {
            m_blinkTime = -1.0f;
            blink = 0.0f;
            m_blinkWait =
                s.blinkMinSeconds + random01() * std::max(0.0f, s.blinkMaxSeconds - s.blinkMinSeconds);
        }
    }
    else if (!asleep)
    {
        m_blinkWait -= seconds;
        if (m_blinkWait <= 0.0f)
        {
            m_blinkTime = 0.0f;
        }
    }
    m_weights[static_cast<usize>(FaceMorph::BlinkL)] = blink;
    m_weights[static_cast<usize>(FaceMorph::BlinkR)] = blink;

    // Talking: random mouth shapes cross-faded at visemesPerSecond; silent: the mouth closes.
    const f32 period = s.visemesPerSecond > 0.0f ? 1.0f / s.visemesPerSecond : 1.0f;
    m_visemeTime += seconds;
    while (m_visemeTime >= period) // long steps switch several times
    {
        m_visemeTime -= period;
        m_visemeFrom = m_visemeTo;
        m_visemeWeightFrom = m_visemeWeightTo;
        if (m_talking)
        {
            i32 next = static_cast<i32>(random01() * static_cast<f32>(kVisemes));
            next = std::min(next, static_cast<i32>(kVisemes) - 1);
            m_visemeTo = next == m_visemeFrom ? (next + 1) % static_cast<i32>(kVisemes) : next;
            m_visemeWeightTo = s.talkWeight * (0.6f + 0.4f * random01());
        }
        else
        {
            m_visemeTo = -1;
        }
    }
    const f32 t = std::clamp(m_visemeTime / period, 0.0f, 1.0f);
    for (usize i = 0; i < kVisemes; ++i)
    {
        m_weights[i] = 0.0f;
    }
    if (m_visemeFrom >= 0)
    {
        m_weights[static_cast<usize>(m_visemeFrom)] += m_visemeWeightFrom * (1.0f - t);
    }
    if (m_visemeTo >= 0)
    {
        m_weights[static_cast<usize>(m_visemeTo)] += m_visemeWeightTo * t;
    }
}
} // namespace g7::animation

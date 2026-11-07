#include <g7/render/Device.hpp>
#include <g7/render/PassTimes.hpp>

#include <algorithm>

namespace g7::render
{
f32 smoothPassTime(f32 previous, f32 sample, bool first) noexcept
{
    return first ? sample : previous + (sample - previous) * 0.1f;
}

void PassTimes::begin(Device& device, std::string_view name)
{
    auto it = std::find_if(m_passes.begin(), m_passes.end(), [&](const Pass& p) { return p.name == name; });
    if (it == m_passes.end())
    {
        Pass pass;
        pass.name = std::string(name);
        m_passes.push_back(std::move(pass));
        m_times.push_back({std::string(name), 0.0f});
        it = m_passes.end() - 1;
    }
    Pass& pass = *it;
    if (!pass.created)
    {
        pass.created = true;
        for (rhi::TimerQuery& query : pass.queries)
        {
            if (auto created = device.createTimerQuery())
            {
                query = std::move(created).value();
            }
        }
    }
    if (pass.issued[pass.next])
    {
        // Still in flight from kRing frames ago: skip this frame's measurement rather than wait.
        m_open = nullptr;
        return;
    }
    device.beginTimerQuery(pass.queries[pass.next]);
    m_open = &pass;
}

void PassTimes::end(Device& device)
{
    if (m_open == nullptr)
    {
        return;
    }
    device.endTimerQuery();
    m_open->issued[m_open->next] = true;
    m_open->next = (m_open->next + 1) % kRing;
    m_open = nullptr;
}

void PassTimes::collect(Device& device)
{
    for (usize i = 0; i < m_passes.size(); ++i)
    {
        Pass& pass = m_passes[i];
        for (usize q = 0; q < kRing; ++q)
        {
            if (!pass.issued[q])
            {
                continue;
            }
            if (const auto ns = device.timerQueryNanoseconds(pass.queries[q]))
            {
                pass.issued[q] = false;
                const f32 ms = static_cast<f32>(*ns) / 1.0e6f;
                PassTime& time = m_times[i];
                time.milliseconds = smoothPassTime(time.milliseconds, ms, time.milliseconds == 0.0f);
            }
        }
    }
}

f32 PassTimes::milliseconds(std::string_view name) const noexcept
{
    const auto it =
        std::find_if(m_times.begin(), m_times.end(), [&](const PassTime& t) { return t.name == name; });
    return it == m_times.end() ? 0.0f : it->milliseconds;
}
} // namespace g7::render

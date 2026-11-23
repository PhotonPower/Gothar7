#include <g7/world/EventNet.hpp>

#include <algorithm>

namespace g7::world
{
void EventNet::clear()
{
    m_relays.clear();
    m_queue.clear();
    m_nextStep.clear();
    m_delayed.clear();
    m_time = 0.0;
    m_step = 0;
    m_order = 0;
}

void EventNet::addRelay(std::string name, const Relay& relay)
{
    RelayRuntime& r = m_relays[std::move(name)];
    r.config = relay;
}

void EventNet::addSender(std::string_view name, std::span<const std::string> targets)
{
    for (const std::string& target : targets)
    {
        if (const auto it = m_relays.find(target); it != m_relays.end())
        {
            it->second.wired.emplace(name);
        }
    }
}

std::vector<std::string> EventNet::cycles() const
{
    std::vector<std::string> result;
    for (const auto& [name, relay] : m_relays)
    {
        // Can it reach itself through other relays?
        std::set<std::string_view> seen;
        std::vector<std::string_view> open(relay.config.targets.begin(), relay.config.targets.end());
        bool cycle = false;
        while (!open.empty() && !cycle)
        {
            const std::string_view next = open.back();
            open.pop_back();
            const auto it = m_relays.find(next);
            if (it == m_relays.end() || !seen.insert(next).second)
            {
                continue;
            }
            cycle = next == name;
            open.insert(open.end(), it->second.config.targets.begin(), it->second.config.targets.end());
        }
        if (cycle)
        {
            result.push_back(name); // the map is sorted by name
        }
    }
    return result;
}

void EventNet::send(std::string_view target, std::string_view action, std::string_view source)
{
    m_queue.push_back({std::string(target), std::string(action), std::string(source)});
}

void EventNet::update(f32 seconds)
{
    ++m_step;
    m_time += static_cast<f64>(seconds);
    // Waiting from the last step first, then delayed events now due (in the order they were sent), then
    // what was sent since.
    std::deque<NetEvent> queue = std::move(m_nextStep);
    m_nextStep.clear();
    std::sort(m_delayed.begin(), m_delayed.end(), [](const Delayed& a, const Delayed& b)
              { return a.due != b.due ? a.due < b.due : a.order < b.order; });
    const auto due =
        std::find_if(m_delayed.begin(), m_delayed.end(), [&](const Delayed& d) { return d.due > m_time; });
    for (auto it = m_delayed.begin(); it != due; ++it)
    {
        queue.push_back(std::move(it->event));
    }
    m_delayed.erase(m_delayed.begin(), due);
    queue.insert(queue.end(), std::make_move_iterator(m_queue.begin()),
                 std::make_move_iterator(m_queue.end()));
    m_queue = std::move(queue);

    for (usize handled = 0; handled < kMaxEventsPerStep && !m_queue.empty(); ++handled)
    {
        const NetEvent event = std::move(m_queue.front());
        m_queue.pop_front();
        handle(event);
    }
}

void EventNet::handle(const NetEvent& event)
{
    const auto it = m_relays.find(event.target);
    if (it == m_relays.end())
    {
        if (m_deliver)
        {
            m_deliver(event);
        }
        return;
    }
    RelayRuntime& relay = it->second;
    if (relay.spent)
    {
        return;
    }
    if (relay.firedStep == m_step)
    {
        m_nextStep.push_back(event); // once per step: a cycle runs one round per step
        return;
    }
    const bool positive = isPositiveEventAction(event.action);
    switch (relay.config.mode)
    {
    case Relay::Mode::Any:
        fire(it->first, relay, positive);
        break;
    case Relay::Mode::All:
    {
        const bool before = allOn(relay);
        if (event.action == "toggle")
        {
            if (!relay.on.erase(event.source))
            {
                relay.on.insert(event.source);
            }
        }
        else if (positive)
        {
            relay.on.insert(event.source);
        }
        else
        {
            relay.on.erase(event.source);
        }
        const bool after = allOn(relay);
        if (before != after)
        {
            fire(it->first, relay, after);
        }
        break;
    }
    case Relay::Mode::Count:
        if (positive && ++relay.counted >= relay.config.count)
        {
            relay.counted = 0;
            fire(it->first, relay, true);
        }
        break;
    }
}

bool EventNet::allOn(const RelayRuntime& relay) const
{
    if (relay.wired.empty())
    {
        return !relay.on.empty(); // only scripts send to it: any input on
    }
    return std::all_of(relay.wired.begin(), relay.wired.end(),
                       [&](const std::string& input) { return relay.on.contains(input); });
}

void EventNet::fire(const std::string& name, RelayRuntime& relay, bool positive)
{
    std::string_view action = relay.config.action;
    if (relay.config.invert)
    {
        action = inverseEventAction(action);
    }
    if (!positive)
    {
        action = inverseEventAction(action);
    }
    relay.firedStep = m_step;
    relay.spent = relay.config.once;
    for (const std::string& target : relay.config.targets)
    {
        NetEvent event{target, std::string(action), name};
        if (relay.config.delay > 0.0f)
        {
            m_delayed.push_back({m_time + static_cast<f64>(relay.config.delay), m_order++, std::move(event)});
        }
        else
        {
            m_queue.push_back(std::move(event));
        }
    }
}

bool EventNet::isRelay(std::string_view name) const
{
    return m_relays.find(name) != m_relays.end();
}

const Relay* EventNet::relay(std::string_view name) const
{
    const auto it = m_relays.find(name);
    return it != m_relays.end() ? &it->second.config : nullptr;
}

RelaySnapshot EventNet::relayState(std::string_view name) const
{
    RelaySnapshot s;
    s.name = std::string(name);
    if (const auto it = m_relays.find(name); it != m_relays.end())
    {
        s.counted = it->second.counted;
        s.inputs.assign(it->second.on.begin(), it->second.on.end());
        s.spent = it->second.spent;
    }
    return s;
}

EventNetSnapshot EventNet::snapshot() const
{
    EventNetSnapshot s;
    for (const auto& [name, relay] : m_relays)
    {
        if (relay.counted > 0 || !relay.on.empty() || relay.spent)
        {
            s.relays.push_back(relayState(name));
        }
    }
    for (const NetEvent& e : m_nextStep)
    {
        s.pending.push_back({e, 0.0f});
    }
    for (const NetEvent& e : m_queue)
    {
        s.pending.push_back({e, 0.0f});
    }
    std::vector<Delayed> delayed = m_delayed;
    std::sort(delayed.begin(), delayed.end(), [](const Delayed& a, const Delayed& b)
              { return a.due != b.due ? a.due < b.due : a.order < b.order; });
    for (const Delayed& d : delayed)
    {
        s.pending.push_back({d.event, std::max(static_cast<f32>(d.due - m_time), 0.0f)});
    }
    return s;
}

std::vector<std::string> EventNet::restore(const EventNetSnapshot& snapshot)
{
    std::vector<std::string> unknown;
    for (auto& [name, relay] : m_relays)
    {
        relay.counted = 0;
        relay.on.clear();
        relay.spent = false;
        relay.firedStep = ~0ull;
    }
    for (const RelaySnapshot& s : snapshot.relays)
    {
        const auto it = m_relays.find(s.name);
        if (it == m_relays.end())
        {
            unknown.push_back(s.name);
            continue;
        }
        it->second.counted = s.counted;
        it->second.on = std::set<std::string>(s.inputs.begin(), s.inputs.end());
        it->second.spent = s.spent;
    }
    m_queue.clear();
    m_nextStep.clear();
    m_delayed.clear();
    for (const PendingNetEvent& p : snapshot.pending)
    {
        if (p.seconds > 0.0f)
        {
            m_delayed.push_back({m_time + static_cast<f64>(p.seconds), m_order++, p.event});
        }
        else
        {
            m_queue.push_back(p.event);
        }
    }
    return unknown;
}
} // namespace g7::world

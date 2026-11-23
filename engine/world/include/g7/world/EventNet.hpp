#pragma once

// The event net of triggers, relays and their targets (docs/modules/dungeon.md "Ereignisse und Ziele", D1;
// like ZenGin's trigger messages): a sender names its targets, an event carries an action ("open", "toggle"
// ...). Relays are handled here; every other target is handed to a callback (the engine: doors, Lua). Pure
// logic in fixed steps - no scene, no scripts.

#include <g7/core/Types.hpp>
#include <g7/world/Components.hpp>

#include <deque>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::world
{
struct NetEvent
{
    std::string target;
    std::string action;
    std::string source; ///< the sender's vob name ("lua" for scripts)
    friend bool operator==(const NetEvent&, const NetEvent&) = default;
};

/// What a relay remembers (saved with the world).
struct RelaySnapshot
{
    std::string name;
    u32 counted = 0;                 ///< Count: events so far
    std::vector<std::string> inputs; ///< All: the inputs that are on, sorted
    bool spent = false;              ///< once: it fired
    friend bool operator==(const RelaySnapshot&, const RelaySnapshot&) = default;
};

/// An event on its way: `seconds` until it arrives (a relay's delay; 0: the next step).
struct PendingNetEvent
{
    NetEvent event;
    f32 seconds = 0.0f;
    friend bool operator==(const PendingNetEvent&, const PendingNetEvent&) = default;
};

/// The net's state for the save game. Relays in their initial state are left out.
struct EventNetSnapshot
{
    std::vector<RelaySnapshot> relays;    ///< sorted by name
    std::vector<PendingNetEvent> pending; ///< in the order they arrive
    friend bool operator==(const EventNetSnapshot&, const EventNetSnapshot&) = default;
};

class EventNet
{
public:
    /// Called for every event whose target is no relay (in the order they happen). It may send() again.
    using Deliver = std::function<void(const NetEvent&)>;

    /// Events handled per step at most (a script answering its own events forever); the rest waits.
    static constexpr usize kMaxEventsPerStep = 256;

    void setDeliver(Deliver deliver) { m_deliver = std::move(deliver); }

    /// Forgets relays, wiring and events (another world).
    void clear();
    /// A relay vob of the loaded world (names are unique per world).
    void addRelay(std::string name, const Relay& relay);
    /// A sender's wiring: its inputs on the relays it names (mode `all` waits for all of them).
    void addSender(std::string_view name, std::span<const std::string> targets);
    /// Relays that lie on a cycle (sender -> relay -> ... -> itself), sorted: the loader warns about them.
    /// Such a cycle runs once per step (a relay fires at most once per step).
    [[nodiscard]] std::vector<std::string> cycles() const;

    /// Queues an event; it is handled in the next update() (or in the running one, when sent from a
    /// delivery).
    void send(std::string_view target, std::string_view action, std::string_view source);
    /// One fixed step: delayed events that are due join the queue, then the queue is worked off. A relay
    /// fires at most once per step; further events for it wait for the next step.
    void update(f32 seconds);

    [[nodiscard]] bool isRelay(std::string_view name) const;
    /// The relay's state, or null for no relay of this name.
    [[nodiscard]] const Relay* relay(std::string_view name) const;
    [[nodiscard]] RelaySnapshot relayState(std::string_view name) const;
    [[nodiscard]] usize pendingCount() const noexcept { return m_queue.size() + m_delayed.size(); }

    [[nodiscard]] EventNetSnapshot snapshot() const;
    /// Puts a snapshot back (after the world's relays were added); unknown relays are skipped and returned.
    std::vector<std::string> restore(const EventNetSnapshot& snapshot);

private:
    struct RelayRuntime
    {
        Relay config;
        std::set<std::string> wired; ///< senders naming it
        std::set<std::string> on;    ///< All: inputs that are on
        u32 counted = 0;
        bool spent = false;
        u64 firedStep = ~0ull; ///< the step it last fired in
    };
    struct Delayed
    {
        f64 due = 0.0;
        u64 order = 0;
        NetEvent event;
    };

    void handle(const NetEvent& event);
    void fire(const std::string& name, RelayRuntime& relay, bool positive);
    [[nodiscard]] bool allOn(const RelayRuntime& relay) const;

    Deliver m_deliver;
    std::map<std::string, RelayRuntime, std::less<>> m_relays;
    std::deque<NetEvent> m_queue;
    std::deque<NetEvent> m_nextStep; ///< for relays that already fired this step
    std::vector<Delayed> m_delayed;
    f64 m_time = 0.0;
    u64 m_step = 0;
    u64 m_order = 0;
};
} // namespace g7::world

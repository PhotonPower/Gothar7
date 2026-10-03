#pragma once

// Trigger volumes (vob type "trigger"): who enters and leaves them. world reports events through a
// callback; the engine registers it (log in M4, Lua from M7) - world itself never calls scripts.

#include <g7/core/Math.hpp>
#include <g7/world/Components.hpp>

#include <functional>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace g7::world
{
class Scene;

/// Someone a trigger can notice: the player or an NPC (by its vob id; in M4 the camera).
struct TriggerProbe
{
    VobId who;
    Vec3 position{0.0f};
    bool player = true;
};

struct TriggerEvent
{
    enum class Kind : u8
    {
        Leave, ///< sorted before Enter for the same trigger and probe
        Enter,
    };
    Kind kind = Kind::Enter;
    VobId trigger;
    VobId who;
    std::string_view
        function; ///< onEnter or onLeave of the trigger (may be empty); valid during the callback
};

class TriggerSystem
{
public:
    using Callback = std::function<void(const TriggerEvent&)>;

    void setCallback(Callback callback) { m_callback = std::move(callback); }

    /// Checks every probe against every trigger (world transforms must be current) and reports the
    /// changes since the last update, ordered by trigger id, then probe id, Leave before Enter. A probe
    /// missing from `probes` has left every trigger. `once` triggers report their first enter only
    /// (every probe entering in that first update, and their leaves). Returns the events of this update in
    /// that order.
    std::vector<TriggerEvent> update(const Scene& scene, std::span<const TriggerProbe> probes);

    /// Arrival (a world was loaded, a start point taken): every probe counts as already inside the
    /// triggers it stands in, without events - such a trigger fires only after the probe left and came
    /// back, so a start point next to a level change does not bounce the player straight back. Forgets
    /// who was inside before; once-triggers keep their state.
    void prime(const Scene& scene, std::span<const TriggerProbe> probes);

    [[nodiscard]] bool isInside(VobId trigger, VobId who) const;
    /// Forgets who is inside and which once-triggers fired (e.g. after loading another world).
    void reset();
    /// Once-triggers that already fired (kept with a world while another one is loaded).
    [[nodiscard]] const std::set<u64>& spentTriggers() const noexcept { return m_spent; }
    void setSpentTriggers(std::set<u64> spent) { m_spent = std::move(spent); }

    /// True if `point` lies in the volume placed by `world` (box: in the turned and scaled box; sphere:
    /// radius times the largest scale).
    [[nodiscard]] static bool contains(const TriggerVolume& volume, const Mat4& world,
                                       const Vec3& point) noexcept;

private:
    Callback m_callback;
    std::set<std::pair<u64, u64>> m_inside; // (trigger, who)
    std::set<u64> m_spent;                  // once-triggers that reported their enter
};
} // namespace g7::world

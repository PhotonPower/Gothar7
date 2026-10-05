// Combat (M11 part A, docs/modules/gameplay.md "Kampf"): values from data/combat.lua, the damage of a hit
// (Gothic 1: weapon + strength - protection, at least a minimum; critical hits by talent) and the state
// machine of a fighter - attacks with their hit window and combos by talent, parry, dodge, stagger, knocked
// out, dead. Animation events drive it (hit_start, hit_end, combo_start, combo_end); while a clip is missing
// a fixed timeline stands in.
#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace g7::script
{
struct Table;
}

namespace g7::gameplay
{
/// Combat values (owner decisions K2-K7, data/combat.lua `Combat`). Per talent level 0, 1, 2.
struct CombatSettings
{
    i32 minDamage = 5;                               ///< K2: every hit that is not parried does at least this
    std::array<f32, 3> critChance{0.0f, 0.1f, 0.2f}; ///< K3: per talent level
    f32 critFactor = 2.0f;                           ///< K3: on the weapon's damage
    std::array<u32, 3> comboHits{1, 3, 4};           ///< K4: hits in a row per talent level
    std::array<f32, 3> attackSpeed{1.0f, 1.0f, 1.25f}; ///< K4: clip rate per talent level
    f32 parrySeconds = 0.4f;                           ///< K6: a parry blocks this long from its start
    f32 parryAngleDegrees = 60.0f;                     ///< K6: hits from within this angle of the front
    f32 knockoutSeconds = 30.0f;                       ///< K7
    f32 staggerSeconds = 0.5f;                         ///< a hit interrupts (without a clip: this long)
    f32 fistReach = 0.9f;                              ///< m from the body's front
    f32 reach1h = 1.3f;
    f32 reach2h = 1.7f;
    f32 hitAngleDegrees = 50.0f; ///< half-angle of the swing in front of the attacker
    // Ranged (M11 part E, owner decisions R1-R4).
    f32 projectileSpeed = 40.0f;                       ///< m/s
    f32 missSpreadDegrees = 5.0f;                      ///< R4: a missed shot goes this far aside
    std::array<f32, 3> bowHitChance{0.3f, 0.6f, 0.9f}; ///< R4: on the focused target, per talent level
    std::array<f32, 3> crossbowHitChance{0.3f, 0.6f, 0.9f};
    f32 bowReload = 1.0f; ///< s between shots (reloading is automatic, R2)
    f32 crossbowReload = 1.6f;
    std::string bowAmmo = "it_arrow";
    std::string crossbowAmmo = "it_bolt";

    /// `Combat` of data/combat.lua; missing values keep their defaults, wrong ones are errors.
    [[nodiscard]] static Result<CombatSettings> fromTable(const script::Table& table);
};

/// Damage by type (Item.damage: edge, blunt, point, fire, magic).
using DamageByType = std::map<std::string, i32, std::less<>>;

struct DamageResult
{
    i32 damage = 0;
    bool critical = false;
};

/// The damage of a melee hit (K2, K3). Per type max(weapon - protection, 0), the attacker's strength added to
/// the weapon's main type (its largest), a critical hit (`roll` < the talent's chance) multiplies the
/// weapon's damage; the sum at least `minDamage`. Fists are blunt 0.
[[nodiscard]] DamageResult meleeDamage(const DamageByType& weapon, i32 strength,
                                       const std::function<i32(std::string_view type)>& protection,
                                       i32 talentLevel, f32 roll, const CombatSettings& settings);

/// R3: the damage of a projectile - per type max(weapon - protection, 0), no strength, no critical hit; at
/// least `minDamage`.
[[nodiscard]] i32 rangedDamage(const DamageByType& weapon,
                               const std::function<i32(std::string_view type)>& protection,
                               const CombatSettings& settings);

/// The launch direction (unit) for `speed` that hits `to` from `from` under gravity - the flat of the two
/// arcs; nullopt when out of reach.
[[nodiscard]] std::optional<Vec3> ballisticDirection(const Vec3& from, const Vec3& to, f32 speed) noexcept;

/// K6: the defender at `defender` looking along `defenderYaw` faces `attacker` within `angleDegrees`.
/// Positions on the ground (x, z); yaw 0 = looking along -Z, positive turns left (as the movement).
[[nodiscard]] bool facesAttacker(f32 defenderYaw, const Vec2& defender, const Vec2& attacker,
                                 f32 angleDegrees) noexcept;

enum class FightState : u8
{
    Ready,
    Attack,
    Parry,
    Dodge,
    Stagger,
    Down, ///< knocked out (K7)
    Dead,
};

enum class AttackKind : u8
{
    Front, ///< the combo
    Left,
    Right,
};

/// One fighter's moves. The engine starts a move, plays clip() and passes the clip's events; without the clip
/// it calls useTimeline() and the fighter times itself.
class Fighter
{
public:
    /// Starts an attack, from Ready or - for the next combo hit - within the combo window (K4: as many as the
    /// talent allows). False if not possible now.
    bool attack(AttackKind kind, i32 talentLevel, const CombatSettings& settings);
    /// Parry (K6) from Ready or within the combo window.
    bool parry();
    bool dodge();
    /// A hit landed on this fighter: an attack or parry is broken off.
    void stagger();
    void knockOut(f32 seconds);
    void die();
    /// Back to Ready (standing up after a knock-out ends here too).
    void reset() noexcept;

    /// Clip events: hit_start, hit_end, combo_start, combo_end.
    void onEvent(std::string_view event);
    /// The move's clip has ended.
    void onClipDone();
    /// The current move has no clip: time it with the fallback timeline (seconds at rate 1).
    void useTimeline();
    void update(f32 seconds, const CombatSettings& settings);

    [[nodiscard]] FightState state() const noexcept { return m_state; }
    [[nodiscard]] AttackKind attackKind() const noexcept { return m_kind; }
    /// 1-based number of the hit in the current combo.
    [[nodiscard]] u32 comboHit() const noexcept { return m_comboHit; }
    /// Clip rate of the current move (K4: faster at talent 2).
    [[nodiscard]] f32 rate() const noexcept { return m_rate; }
    /// Between hit_start and hit_end of an attack.
    [[nodiscard]] bool hitWindow() const noexcept { return m_state == FightState::Attack && m_hitting; }
    /// A new swing started since the last call (the engine clears the set of targets hit).
    [[nodiscard]] bool takeNewSwing() noexcept;
    [[nodiscard]] bool parrying(const CombatSettings& settings) const noexcept;
    /// Seconds in the current state.
    [[nodiscard]] f32 seconds() const noexcept { return m_seconds; }
    /// The clip for the state in weapon mode `mode` ("fist", "1h", "2h"): "1h/t_attack_combo2",
    /// "fist/t_parry", "none/t_ko" ...; empty for Ready.
    [[nodiscard]] std::string clip(std::string_view mode) const;

    // The fallback timeline (seconds at rate 1): hit window, combo window, end.
    static constexpr f32 kTimelineHitStart = 0.25f;
    static constexpr f32 kTimelineHitEnd = 0.45f;
    static constexpr f32 kTimelineComboEnd = 0.7f;
    static constexpr f32 kTimelineAttackEnd = 0.9f;
    static constexpr f32 kTimelineParryEnd = 0.6f;
    static constexpr f32 kTimelineDodgeEnd = 0.5f;

private:
    void enter(FightState state) noexcept;

    FightState m_state = FightState::Ready;
    AttackKind m_kind = AttackKind::Front;
    u32 m_comboHit = 0;
    u32 m_comboMax = 1;
    bool m_hitting = false;
    bool m_comboOpen = false;
    bool m_newSwing = false;
    bool m_timeline = false;
    f32 m_rate = 1.0f;
    f32 m_seconds = 0.0f;
    f32 m_limit = 0.0f; ///< Down/Stagger: seconds until Ready
};
} // namespace g7::gameplay

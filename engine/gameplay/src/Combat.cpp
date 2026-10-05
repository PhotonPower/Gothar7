#include <g7/gameplay/Combat.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/script/Value.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7::gameplay
{
namespace
{
/// A number field of the table: missing keeps `value`, a wrong one is an error.
Result<void> readNumber(const script::Table& t, const char* key, f32& value, f64 min, f64 max)
{
    const auto it = t.fields.find(key);
    if (it == t.fields.end())
    {
        return {};
    }
    if (!it->second.isNumber() || it->second.asNumber() < min || it->second.asNumber() > max)
    {
        return Error{std::format("Combat.{}: must be a number {}..{}", key, min, max)};
    }
    value = static_cast<f32>(it->second.asNumber());
    return {};
}

/// Three values per talent level ({0, 0.1, 0.2}).
template <class T>
Result<void> readLevels(const script::Table& t, const char* key, std::array<T, 3>& values, f64 min, f64 max)
{
    const auto it = t.fields.find(key);
    if (it == t.fields.end())
    {
        return {};
    }
    const script::Table* levels = it->second.asTable();
    if (levels == nullptr || levels->array.size() != 3)
    {
        return Error{std::format("Combat.{}: needs three values (talent level 0, 1, 2)", key)};
    }
    for (usize i = 0; i < 3; ++i)
    {
        const script::Value& v = levels->array[i];
        if (!v.isNumber() || v.asNumber() < min || v.asNumber() > max)
        {
            return Error{std::format("Combat.{}[{}]: must be a number {}..{}", key, i + 1, min, max)};
        }
        values[i] = static_cast<T>(v.asNumber());
    }
    return {};
}

usize level(i32 talent) noexcept
{
    return static_cast<usize>(std::clamp(talent, 0, 2));
}
} // namespace

Result<CombatSettings> CombatSettings::fromTable(const script::Table& t)
{
    CombatSettings s;
    f32 minDamage = static_cast<f32>(s.minDamage);
    for (auto r :
         {readNumber(t, "min_damage", minDamage, 0.0, 1000.0),
          readNumber(t, "crit_factor", s.critFactor, 1.0, 10.0),
          readNumber(t, "parry_seconds", s.parrySeconds, 0.0, 5.0),
          readNumber(t, "parry_angle", s.parryAngleDegrees, 0.0, 180.0),
          readNumber(t, "knockout_seconds", s.knockoutSeconds, 0.0, 600.0),
          readNumber(t, "stagger_seconds", s.staggerSeconds, 0.0, 5.0),
          readNumber(t, "fist_reach", s.fistReach, 0.1, 5.0), readNumber(t, "reach_1h", s.reach1h, 0.1, 5.0),
          readNumber(t, "reach_2h", s.reach2h, 0.1, 5.0),
          readNumber(t, "hit_angle", s.hitAngleDegrees, 1.0, 180.0),
          readLevels(t, "crit_chance", s.critChance, 0.0, 1.0),
          readLevels(t, "combo_hits", s.comboHits, 1.0, 10.0),
          readLevels(t, "attack_speed", s.attackSpeed, 0.25, 4.0),
          readNumber(t, "projectile_speed", s.projectileSpeed, 5.0, 200.0),
          readNumber(t, "miss_spread", s.missSpreadDegrees, 0.0, 45.0),
          readNumber(t, "bow_reload", s.bowReload, 0.1, 10.0),
          readNumber(t, "crossbow_reload", s.crossbowReload, 0.1, 10.0),
          readLevels(t, "bow_hit_chance", s.bowHitChance, 0.0, 1.0),
          readLevels(t, "crossbow_hit_chance", s.crossbowHitChance, 0.0, 1.0)})
    {
        if (!r)
        {
            return r.error();
        }
    }
    s.minDamage = static_cast<i32>(minDamage);
    for (const auto& [key, target] :
         {std::pair{"bow_ammo", &s.bowAmmo}, std::pair{"crossbow_ammo", &s.crossbowAmmo}})
    {
        if (const auto it = t.fields.find(key); it != t.fields.end())
        {
            if (!it->second.isString())
            {
                return Error{std::format("Combat.{}: must be an Item name", key)};
            }
            *target = std::string(it->second.asString());
        }
    }
    return s;
}

DamageResult meleeDamage(const DamageByType& weapon, i32 strength,
                         const std::function<i32(std::string_view type)>& protection, i32 talentLevel,
                         f32 roll, const CombatSettings& settings)
{
    DamageResult result;
    result.critical = roll < settings.critChance[level(talentLevel)];
    // The weapon's main type gets the strength: its largest (fists: blunt).
    std::string main = "blunt";
    i32 largest = -1;
    for (const auto& [type, value] : weapon)
    {
        if (value > largest)
        {
            largest = value;
            main = type;
        }
    }
    DamageByType hit = weapon;
    i32 total = 0;
    if (!hit.contains(main))
    {
        hit[main] = 0;
    }
    for (const auto& [type, value] : hit)
    {
        const f32 weaponPart = static_cast<f32>(value) * (result.critical ? settings.critFactor : 1.0f);
        const i32 raw = static_cast<i32>(std::lround(weaponPart)) + (type == main ? strength : 0);
        total += std::max(raw - protection(type), 0);
    }
    result.damage = std::max(total, settings.minDamage);
    return result;
}

i32 rangedDamage(const DamageByType& weapon, const std::function<i32(std::string_view type)>& protection,
                 const CombatSettings& settings)
{
    i32 total = 0;
    for (const auto& [type, value] : weapon)
    {
        total += std::max(value - protection(type), 0);
    }
    return std::max(total, settings.minDamage);
}

std::optional<Vec3> ballisticDirection(const Vec3& from, const Vec3& to, f32 speed) noexcept
{
    const Vec3 d = to - from;
    const f32 x = glm::length(Vec2(d.x, d.z));
    const f32 y = d.y;
    if (x < 1e-3f)
    {
        return glm::length(d) > 1e-3f ? std::optional<Vec3>(glm::normalize(d)) : std::nullopt;
    }
    constexpr f32 g = kGravity;
    const f32 v2 = speed * speed;
    const f32 disc = v2 * v2 - g * (g * x * x + 2.0f * y * v2);
    if (disc < 0.0f)
    {
        return std::nullopt;
    }
    const f32 angle = std::atan2(v2 - std::sqrt(disc), g * x); // the flat arc
    const Vec2 horizontal = Vec2(d.x, d.z) / x;
    return Vec3(horizontal.x * std::cos(angle), std::sin(angle), horizontal.y * std::cos(angle));
}

bool facesAttacker(f32 defenderYaw, const Vec2& defender, const Vec2& attacker, f32 angleDegrees) noexcept
{
    const Vec2 to = attacker - defender;
    if (glm::length(to) < 1e-4f)
    {
        return true;
    }
    const Vec3 forward = forwardOf(defenderYaw);
    const f32 cosine = glm::dot(glm::normalize(to), glm::normalize(Vec2(forward.x, forward.z)));
    return cosine >= std::cos(glm::radians(angleDegrees));
}

void Fighter::enter(FightState state) noexcept
{
    m_state = state;
    m_seconds = 0.0f;
    m_hitting = false;
    m_comboOpen = false;
    m_timeline = false;
}

bool Fighter::attack(AttackKind kind, i32 talentLevel, const CombatSettings& settings)
{
    const usize l = level(talentLevel);
    if (m_state == FightState::Attack && m_comboOpen && kind == AttackKind::Front &&
        m_kind == AttackKind::Front && m_comboHit < m_comboMax)
    {
        const u32 next = m_comboHit + 1;
        enter(FightState::Attack);
        m_comboHit = next;
        m_newSwing = true;
        return true;
    }
    if (m_state != FightState::Ready)
    {
        return false;
    }
    enter(FightState::Attack);
    m_kind = kind;
    m_comboHit = 1;
    m_comboMax = kind == AttackKind::Front ? settings.comboHits[l] : 1;
    m_rate = settings.attackSpeed[l];
    m_newSwing = true;
    return true;
}

bool Fighter::parry()
{
    if (m_state != FightState::Ready && !(m_state == FightState::Attack && m_comboOpen))
    {
        return false;
    }
    enter(FightState::Parry);
    m_rate = 1.0f;
    return true;
}

bool Fighter::dodge()
{
    if (m_state != FightState::Ready && !(m_state == FightState::Attack && m_comboOpen))
    {
        return false;
    }
    enter(FightState::Dodge);
    m_rate = 1.0f;
    return true;
}

void Fighter::stagger()
{
    if (m_state == FightState::Down || m_state == FightState::Dead)
    {
        return;
    }
    enter(FightState::Stagger);
    m_rate = 1.0f;
}

void Fighter::knockOut(f32 seconds)
{
    enter(FightState::Down);
    m_limit = seconds;
}

void Fighter::die()
{
    enter(FightState::Dead);
}

void Fighter::reset() noexcept
{
    enter(FightState::Ready);
    m_comboHit = 0;
}

void Fighter::onEvent(std::string_view event)
{
    if (m_state != FightState::Attack)
    {
        return;
    }
    if (event == "hit_start")
    {
        m_hitting = true;
    }
    else if (event == "hit_end")
    {
        m_hitting = false;
    }
    else if (event == "combo_start")
    {
        m_comboOpen = m_kind == AttackKind::Front && m_comboHit < m_comboMax;
    }
    else if (event == "combo_end")
    {
        m_comboOpen = false;
    }
}

void Fighter::onClipDone()
{
    if (m_state == FightState::Attack || m_state == FightState::Parry || m_state == FightState::Dodge ||
        m_state == FightState::Stagger)
    {
        reset();
    }
}

void Fighter::useTimeline()
{
    m_timeline = true;
}

void Fighter::update(f32 seconds, const CombatSettings& settings)
{
    const f32 before = m_seconds;
    m_seconds += seconds * (m_state == FightState::Attack ? m_rate : 1.0f);
    const auto passed = [&](f32 t) { return before < t && m_seconds >= t; };
    switch (m_state)
    {
    case FightState::Attack:
        if (m_timeline)
        {
            if (passed(kTimelineHitStart))
            {
                onEvent("hit_start");
            }
            if (passed(kTimelineHitEnd))
            {
                onEvent("hit_end");
                onEvent("combo_start");
            }
            if (passed(kTimelineComboEnd))
            {
                onEvent("combo_end");
            }
            if (m_seconds >= kTimelineAttackEnd)
            {
                reset();
            }
        }
        break;
    case FightState::Parry:
        if (m_timeline && m_seconds >= kTimelineParryEnd)
        {
            reset();
        }
        break;
    case FightState::Dodge:
        if (m_timeline && m_seconds >= kTimelineDodgeEnd)
        {
            reset();
        }
        break;
    case FightState::Stagger:
        if (m_timeline && m_seconds >= settings.staggerSeconds)
        {
            reset();
        }
        break;
    case FightState::Down:
        if (m_seconds >= m_limit)
        {
            reset(); // stands up (the engine plays t_ko_getup)
        }
        break;
    case FightState::Ready:
    case FightState::Dead:
        break;
    }
}

bool Fighter::takeNewSwing() noexcept
{
    const bool swing = m_newSwing;
    m_newSwing = false;
    return swing;
}

bool Fighter::parrying(const CombatSettings& settings) const noexcept
{
    return m_state == FightState::Parry && m_seconds <= settings.parrySeconds;
}

std::string Fighter::clip(std::string_view mode) const
{
    switch (m_state)
    {
    case FightState::Attack:
        if (m_kind == AttackKind::Left)
        {
            return std::format("{}/t_attack_l", mode);
        }
        if (m_kind == AttackKind::Right)
        {
            return std::format("{}/t_attack_r", mode);
        }
        return std::format("{}/t_attack_combo{}", mode, m_comboHit);
    case FightState::Parry:
        return std::format("{}/t_parry", mode);
    case FightState::Dodge:
        return std::format("{}/t_dodge_back", mode);
    case FightState::Stagger:
        return "none/t_hit_light";
    case FightState::Down:
        return "none/t_ko";
    case FightState::Dead:
        return "none/t_die_front";
    case FightState::Ready:
        break;
    }
    return {};
}
} // namespace g7::gameplay

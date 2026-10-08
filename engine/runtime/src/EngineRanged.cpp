// Ranged combat (M11 part E, owner decisions R1-R4): the hero draws his bow or crossbow (draw_ranged; the
// draw key takes it when there is no melee weapon), shoots with the fighting keys - at the locked or focused
// target with a hit chance by talent (a miss goes 5 degrees aside), else freely along the view -, reloads by
// himself while he has ammunition. Projectiles fly with gravity; what they hit takes the weapon's point
// damage minus its protection (no strength, no critical hit) and keeps the arrow; missed ones lie where they
// land.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Mobs.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace g7
{
namespace
{
using script::Value;

constexpr u8 kRangedMode = 3;         ///< m_weaponMode: bow or crossbow drawn
constexpr f32 kProjectileLife = 6.0f; ///< s, then it is gone
constexpr f32 kHitRadius = 0.4f;      ///< m around a body's axis
constexpr f32 kHumanHeight = 1.8f;
constexpr f32 kAnimalHeight = 1.0f;
constexpr u32 kHeroShooter = ~u32(0);

/// Shortest distance between the segments [p0, p1] and [q0, q1].
f32 segmentDistance(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1)
{
    const Vec3 d1 = p1 - p0;
    const Vec3 d2 = q1 - q0;
    const Vec3 r = p0 - q0;
    const f32 a = glm::dot(d1, d1);
    const f32 e = glm::dot(d2, d2);
    const f32 f = glm::dot(d2, r);
    f32 s = 0.0f;
    f32 t = 0.0f;
    if (a <= 1e-8f && e <= 1e-8f)
    {
        return glm::length(r);
    }
    if (a <= 1e-8f)
    {
        t = std::clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const f32 c = glm::dot(d1, r);
        if (e <= 1e-8f)
        {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const f32 b = glm::dot(d1, d2);
            const f32 denom = a * e - b * b;
            s = denom != 0.0f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f)
            {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    return glm::length((p0 + d1 * s) - (q0 + d2 * t));
}
} // namespace

std::string Engine::rangedWeapon() const
{
    const gameplay::Character* h = hero();
    return h != nullptr ? h->equipped(gameplay::EquipSlot::Ranged) : std::string();
}

bool Engine::rangedIsCrossbow(std::string_view item) const
{
    const script::Instance* def =
        m_scripts && !item.empty() ? m_scripts->findInstance("Item", item) : nullptr;
    return def != nullptr && def->fields["category"].asString() == "crossbow";
}

void Engine::toggleRanged()
{
    if (!m_figure)
    {
        return;
    }
    if (m_weaponMode != 0)
    {
        toggleWeapon(); // whatever is drawn goes away first
        return;
    }
    const std::string weapon = rangedWeapon();
    if (weapon.empty())
    {
        notice("Kein Bogen und keine Armbrust ausgerüstet.");
        return;
    }
    m_weaponMode = kRangedMode;
    m_weaponDrawn = weapon;
    m_rangedReload = 0.0f;
    weaponEvent("draw"); // no bow clips yet: in the hand at once
}

std::optional<Vec3> Engine::aimPoint(u32 creatureId) const
{
    const Creature* c = creature(creatureId);
    if (c == nullptr)
    {
        return std::nullopt;
    }
    const bool animal = c->character && m_scripts && m_scripts->findInstance("Npc", c->species) != nullptr &&
                        !m_scripts->findInstance("Npc", c->species)->fields["species"].asString().empty();
    return c->position + Vec3(0.0f, animal ? 0.5f : 1.3f, 0.0f);
}

Result<void> Engine::shootRanged()
{
    if (!m_player.valid() || !m_hero || m_weaponMode != kRangedMode || m_weaponDrawn.empty())
    {
        return Error{"no bow or crossbow drawn"};
    }
    if (m_rangedReload > 1e-3f) // (float steps leave a remainder)
    {
        return Error{"still reloading"};
    }
    const bool crossbow = rangedIsCrossbow(m_weaponDrawn);
    const std::string& ammo = crossbow ? m_combat.crossbowAmmo : m_combat.bowAmmo;
    if (!m_hero->removeItem(ammo, 1))
    {
        notice(crossbow ? "Keine Bolzen." : "Keine Pfeile."); // R2
        return Error{std::format("no {}", ammo)};
    }
    m_rangedReload = crossbow ? m_combat.crossbowReload : m_combat.bowReload; // R2: reloads by himself

    const Vec3 forward = gameplay::forwardOf(m_movement.yaw());
    const Vec3 origin = m_player.feet() + Vec3(0.0f, 1.5f, 0.0f) + forward * 0.6f;
    Vec3 direction;
    std::optional<u32> target = m_combatTarget;
    if (!target && m_focus && m_focus->kind == gameplay::FocusKind::Npc)
    {
        target = static_cast<u32>(m_focus->id & ~(1ull << 62));
    }
    const std::optional<Vec3> aim = target ? aimPoint(*target) : std::nullopt;
    if (aim)
    {
        // R4: at the focused target a hit by talent; a miss goes a little aside.
        const i32 talent = std::clamp(m_hero->talent(crossbow ? "crossbow" : "bow"), 0, 2);
        const f32 chance = (crossbow ? m_combat.crossbowHitChance : m_combat.bowHitChance)[talent];
        const f32 roll = m_random ? m_random() : std::uniform_real_distribution<f32>(0.0f, 1.0f)(m_rng);
        direction = gameplay::ballisticDirection(origin, *aim, m_combat.projectileSpeed)
                        .value_or(glm::normalize(*aim - origin));
        if (roll >= chance)
        {
            const f32 side = (std::uniform_real_distribution<f32>(0.0f, 1.0f)(m_rng) < 0.5f ? -1.0f : 1.0f) *
                             glm::radians(m_combat.missSpreadDegrees);
            direction = glm::angleAxis(side, Vec3(0.0f, 1.0f, 0.0f)) * direction;
        }
    }
    else
    {
        // Free: along the view (the camera looks over the hero's shoulder).
        direction = m_camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    }
    Projectile p;
    p.position = origin;
    p.velocity = glm::normalize(direction) * m_combat.projectileSpeed;
    p.ammo = ammo;
    p.shooter = kHeroShooter;
    const script::Instance* weapon = m_scripts ? m_scripts->findInstance("Item", m_weaponDrawn) : nullptr;
    if (weapon != nullptr)
    {
        if (const script::Table* t = weapon->fields["damage"].asTable())
        {
            for (const auto& [type, value] : t->fields)
            {
                p.damage[type] = static_cast<i32>(value.asInteger());
            }
        }
    }
    m_projectiles.push_back(std::move(p));
    if (m_scripts)
    {
        const Value args[] = {std::string("hero"), ammo};
        m_scripts->emit("npc_shot", args);
    }
    return {};
}

void Engine::fixedUpdateProjectiles(f32 seconds)
{
    m_rangedReload = std::max(0.0f, m_rangedReload - seconds);
    for (Projectile& p : m_projectiles)
    {
        p.seconds += seconds;
        const Vec3 from = p.position;
        if (p.spell.empty())
        {
            p.velocity.y -= gameplay::kGravity * seconds; // a spell flies straight (M12)
        }
        const Vec3 to = from + p.velocity * seconds;
        p.position = to;
        if (p.trail)
        {
            m_particles.move(*p.trail, to, -glm::normalize(p.velocity));
        }
        // What it flies into first: a body or the world.
        f32 nearest = glm::length(to - from);
        std::optional<u32> body;
        bool heroHit = false;
        for (const auto& c : m_creatures)
        {
            if (!c->character || c->id == p.shooter || c->fighter.state() == gameplay::FightState::Dead)
            {
                continue;
            }
            const auto point = aimPoint(c->id);
            const f32 height = point && point->y - c->position.y < 1.0f ? kAnimalHeight : kHumanHeight;
            if (segmentDistance(from, to, c->position, c->position + Vec3(0.0f, height, 0.0f)) < kHitRadius)
            {
                const f32 along = glm::length(c->position + Vec3(0.0f, height * 0.5f, 0.0f) - from);
                if (along <= nearest + kHitRadius)
                {
                    nearest = along;
                    body = c->id;
                }
            }
        }
        if (p.shooter != kHeroShooter && m_player.valid() &&
            segmentDistance(from, to, m_player.feet(), m_player.feet() + Vec3(0.0f, kHumanHeight, 0.0f)) <
                kHitRadius)
        {
            heroHit = true;
        }
        const Vec3 dir = to - from;
        const f32 length = glm::length(dir);
        const auto wall =
            m_physics.valid() && length > 1e-4f
                ? m_physics.raycast(from, dir / length, length, physics::layerBit(physics::Layer::World))
                : std::nullopt;
        if (wall && (!body || wall->distance < nearest) && !heroHit)
        {
            if (!p.spell.empty())
            {
                spellImpact(p, wall->position + wall->normal * 0.1f);
            }
            // R3: it lies where it landed and can be picked up.
            else if (auto item = spawnItem(p.ammo, 1, wall->position + wall->normal * 0.05f,
                                           gameplay::yawOf(Vec3(dir.x, 0.0f, dir.z)));
                     !item)
            {
                G7_LOG_WARN("engine", "projectile: {}", item.error().message);
            }
            p.seconds = kProjectileLife;
            continue;
        }
        if (body || heroHit)
        {
            projectileHit(p, heroHit ? kHeroShooter : *body);
            if (!p.spell.empty())
            {
                spellImpact(p, p.position);
            }
            p.seconds = kProjectileLife;
        }
    }
    for (const Projectile& p : m_projectiles)
    {
        if (p.seconds >= kProjectileLife && p.trail)
        {
            m_particles.stop(*p.trail); // its sparks still glow out
        }
    }
    std::erase_if(m_projectiles, [](const Projectile& p) { return p.seconds >= kProjectileLife; });
}

void Engine::spellImpact(const Projectile& p, const Vec3& at)
{
    if (!p.impact.empty())
    {
        (void)startEffect(p.impact, at);
    }
    if (!p.impactSound.empty())
    {
        (void)playSound(p.impactSound, at); // M13
    }
}

void Engine::drawProjectiles()
{
    for (const Projectile& p : m_projectiles)
    {
        const LoadedModel* model = p.ammo.empty() ? nullptr : itemModel(p.ammo); // spells: their trail only
        if (model == nullptr || glm::length(p.velocity) < 1e-3f)
        {
            continue;
        }
        // +Y of the model along the flight (figuren: origin in the middle of the shaft, +Y to the tip).
        const Vec3 y = glm::normalize(p.velocity);
        const Vec3 helper = std::abs(y.y) < 0.99f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 x = glm::normalize(glm::cross(y, helper));
        const Vec3 z = glm::cross(x, y);
        const Mat4 at(Vec4(x, 0.0f), Vec4(y, 0.0f), Vec4(z, 0.0f), Vec4(p.position, 1.0f));
        m_meshRenderer.draw(*m_device, model->mesh, model->materials, at, m_camera);
    }
}

void Engine::bindRangedFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"draw_ranged", "draw_ranged() -> string",
             "Zieht den ausgerüsteten Bogen bzw. die Armbrust bzw. steckt weg, wie die Taste draw_ranged "
             "(M11, R1); "
             "gibt zurück, was danach gezogen ist (wie player_weapon).",
             "Kampf", [this](std::span<const Value>) -> Result<Value>
             {
                 toggleRanged();
                 return Value(std::string(m_weaponMode == 0             ? "none"
                                          : m_weaponMode == kRangedMode ? "ranged"
                                          : m_weaponMode == 1           ? "weapon"
                                                                        : "fists"));
             }});
    vm.bind({"hero_shoot", "hero_shoot() -> boolean",
             "Ein Schuss des Helden mit gezogenem Bogen bzw. Armbrust (sonst über die Steuerung); false beim "
             "Nachladen "
             "oder ohne Munition.",
             "Kampf", [this](std::span<const Value>) -> Result<Value> { return Value(shootRanged().ok()); }});
    vm.bind({"npc_shot",
             "on(\"npc_shot\", fn(shooter: string, ammo: string))",
             "Ein Schuss (M11, `hero`).",
             "Ereignisse",
             {}});
}
} // namespace g7

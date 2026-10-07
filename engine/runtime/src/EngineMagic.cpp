// Magic (M12, EngineMagic.cpp): spells from runes and scrolls. Part B: what may be cast (Z1-Z3: runes need
// the spell's circle, scrolls not; both cost mana, which does not come back by itself). Part C1: casting by
// the hero (Z4-Z6) - the rune places on keys 4-9, "1" draws the last chosen; the fighting keys held charge a
// spell with stages (1 s each), released cast it; the cast clip's "cast" event lets it act: a fire bolt
// flies, healing heals, sleep lays the target down until hurt.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Magic.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
constexpr u8 kMagicMode = 4;          ///< m_weaponMode: a rune or scroll drawn
constexpr u32 kHeroShooter = ~u32(0); ///< as the hero's id in EngineCombat/EngineRanged
constexpr u64 kCreatureFocusBit = 1ull << 62;
constexpr f32 kCastFallbackSeconds = 0.4f; ///< no cast clip: the spell acts this long after release
constexpr f32 kCastMaxSeconds = 4.0f;      ///< a cast clip that never ends is given up

f32 magicValue(const script::ScriptVm* vm, std::string_view name, f32 fallback)
{
    const script::Value table = vm != nullptr ? vm->global("Magic") : script::Value();
    const script::Table* t = table.asTable();
    if (t == nullptr)
    {
        return fallback;
    }
    const auto it = t->fields.find(std::string(name));
    return it != t->fields.end() && it->second.isNumber() ? static_cast<f32>(it->second.asNumber())
                                                          : fallback;
}

const char* castState(gameplay::SpellKind kind)
{
    switch (kind)
    {
    case gameplay::SpellKind::Projectile:
        return "mag_t_cast_projectile";
    case gameplay::SpellKind::Area:
        return "mag_t_cast_area";
    case gameplay::SpellKind::Target:
        return "mag_t_cast_target";
    case gameplay::SpellKind::Summon:
        return "mag_t_cast_summon";
    case gameplay::SpellKind::Self:
    case gameplay::SpellKind::Transform:
        break;
    }
    return "mag_t_cast_self";
}
} // namespace

std::string Engine::runeInSlot(u32 slot) const
{
    if (!m_hero || slot >= 7)
    {
        return {};
    }
    return m_hero->equipped(
        static_cast<gameplay::EquipSlot>(static_cast<u8>(gameplay::EquipSlot::Rune1) + slot));
}

void Engine::toggleMagic()
{
    if (!m_figure || !m_hero)
    {
        return;
    }
    if (m_weaponMode != 0)
    {
        m_heroCast.reset();
        toggleWeapon(); // whatever is drawn goes away first
        return;
    }
    if (runeInSlot(m_runeSlot).empty())
    {
        for (u32 slot = 0; slot < 7; ++slot)
        {
            if (!runeInSlot(slot).empty())
            {
                m_runeSlot = slot; // the first rune place with something on it
                break;
            }
        }
    }
    const std::string item = runeInSlot(m_runeSlot);
    if (item.empty())
    {
        notice("Keine Rune und keine Spruchrolle angelegt.");
        return;
    }
    m_weaponMode = kMagicMode;
    m_weaponDrawn = item; // in the hand at the clip's "draw" event
    if (!m_figure->animator.hasState("draw_mag"))
    {
        weaponEvent("draw");
    }
}

void Engine::selectRune(u32 slot)
{
    const std::string item = runeInSlot(slot);
    if (item.empty())
    {
        notice("Auf diesem Runenplatz liegt nichts.");
        return;
    }
    m_runeSlot = slot;
    if (m_weaponMode == kMagicMode)
    {
        // Another rune in the hand at once (Gothic 1).
        m_heroCast.reset();
        m_weaponDrawn = item;
        detachFromPlayer("socket_hand_r");
        weaponEvent("draw");
    }
    else if (m_weaponMode == 0)
    {
        toggleMagic();
    }
    else
    {
        toggleWeapon(); // the weapon away first; the key again draws the rune
    }
}

void Engine::beginHeroCast()
{
    const auto spell = spellOfItem(m_weaponDrawn);
    if (!spell || !m_hero)
    {
        return;
    }
    const script::Instance* def = m_scripts->findInstance("Item", m_weaponDrawn);
    const bool scroll = def != nullptr && def->fields["category"].asString() == "scroll";
    if (spell->kind == gameplay::SpellKind::Summon || spell->kind == gameplay::SpellKind::Transform)
    {
        notice("Dieser Zauber lässt sich noch nicht wirken."); // M12 part C2
        return;
    }
    if (const auto blocked = gameplay::castBlocked(*m_hero, *spell, scroll); blocked)
    {
        notice(*blocked); // Z5: the attempt fails
        if (m_figure && m_figure->animator.hasState("mag_t_cast_fail"))
        {
            m_figure->animator.enter("mag_t_cast_fail", 0.1f);
        }
        return;
    }
    HeroCast cast;
    cast.item = m_weaponDrawn;
    cast.spell = *spell;
    cast.scroll = scroll;
    cast.target = m_combatTarget;
    if (!cast.target && m_focus && m_focus->kind == gameplay::FocusKind::Npc)
    {
        cast.target = static_cast<u32>(m_focus->id & ~kCreatureFocusBit);
    }
    cast.charging = spell->investStages > 0;
    m_heroCast = std::move(cast);
    if (m_heroCast->charging)
    {
        if (m_figure && m_figure->animator.hasState("mag_t_invest"))
        {
            m_figure->animator.enter("mag_t_invest", 0.1f);
        }
        return;
    }
    releaseHeroCast(); // a simple spell acts at once (Z5)
}

void Engine::releaseHeroCast()
{
    if (!m_heroCast || m_heroCast->cast || !m_hero)
    {
        return;
    }
    HeroCast& cast = *m_heroCast;
    cast.charging = false;
    cast.cast = true;
    cast.seconds = 0.0f;
    const i32 mana = gameplay::spellMana(cast.spell, cast.stages);
    (void)m_hero->setAttribute("mana", m_hero->attribute("mana") - mana);
    if (cast.scroll)
    {
        (void)m_hero->removeItem(cast.item, 1); // Z3: a scroll is used up
    }
    const char* state = castState(cast.spell.kind);
    if (m_figure && m_figure->animator.hasState(state))
    {
        m_figure->animator.enter(state, 0.1f);
        cast.state = state;
    }
    G7_LOG_INFO("engine", "hero casts {} (stage {}, {} mana)", cast.spell.instance, cast.stages, mana);
    if (m_scripts)
    {
        const script::Value args[] = {std::string("hero"), cast.spell.instance};
        m_scripts->emit("npc_cast", args);
    }
}

void Engine::applyHeroSpell()
{
    if (!m_heroCast || !m_heroCast->cast || m_heroCast->acted || !m_hero || !m_player.valid())
    {
        return;
    }
    m_heroCast->acted = true;
    const gameplay::SpellInfo& spell = m_heroCast->spell;
    const u32 stages = m_heroCast->stages;
    const f32 strength = 1.0f + static_cast<f32>(stages); // Z5: each stage as much again
    const Vec3 forward = gameplay::forwardOf(m_movement.yaw());
    const Vec3 hand = m_player.feet() + Vec3(0.0f, 1.4f, 0.0f) + forward * 0.5f;
    const auto fx = [&](std::string_view role) -> std::string
    {
        const auto it = spell.fx.find(role);
        return it != spell.fx.end() ? it->second : std::string();
    };
    if (const std::string cast = fx("cast"); !cast.empty())
    {
        (void)startEffect(cast, hand);
    }
    const Creature* target = m_heroCast->target ? creature(*m_heroCast->target) : nullptr;
    switch (spell.kind)
    {
    case gameplay::SpellKind::Projectile:
    {
        // Straight at the locked or focused target, else along the view; no gravity, no miss roll.
        const auto aim = target != nullptr ? aimPoint(target->id) : std::nullopt;
        const Vec3 direction = aim ? glm::normalize(*aim - hand)
                                   : glm::normalize(m_camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f));
        Projectile p;
        p.position = hand;
        p.velocity = direction * magicValue(m_scripts.get(), "projectile_speed", 30.0f);
        p.shooter = kHeroShooter;
        p.spell = spell.instance;
        p.stages = stages;
        for (const auto& [type, value] : spell.damage)
        {
            p.damage[type] = static_cast<i32>(std::lround(static_cast<f32>(value) * strength));
        }
        if (const std::string trail = fx("trail"); !trail.empty())
        {
            p.trail = startEffect(trail, hand, -direction);
        }
        p.impact = fx("impact");
        m_projectiles.push_back(std::move(p));
        break;
    }
    case gameplay::SpellKind::Self:
    {
        const i32 heal = static_cast<i32>(std::lround(static_cast<f32>(spell.heal) * strength));
        (void)m_hero->setAttribute("hp",
                                   std::min(m_hero->attribute("hp_max"), m_hero->attribute("hp") + heal));
        if (const std::string on = fx("on_target"); !on.empty())
        {
            (void)startEffect(on, m_player.feet());
        }
        break;
    }
    case gameplay::SpellKind::Target:
    {
        const f32 range = magicValue(m_scripts.get(), "target_range", 25.0f);
        if (target == nullptr || glm::length(target->position - m_player.feet()) > range)
        {
            notice("Kein Ziel.");
            break;
        }
        if (spell.effect == "sleep" &&
            !castSleep(target->id, spell.duration * strength, "hero", fx("on_target")))
        {
            notice("Der Zauber zeigt keine Wirkung."); // Z6: stronger than the caster
        }
        break;
    }
    case gameplay::SpellKind::Area:
    {
        const f32 radius = spell.radius * (1.0f + 0.5f * static_cast<f32>(stages));
        gameplay::DamageByType damage;
        for (const auto& [type, value] : spell.damage)
        {
            damage[type] = static_cast<i32>(std::lround(static_cast<f32>(value) * strength));
        }
        std::vector<u32> hit;
        for (const auto& c : m_creatures)
        {
            if (c->character && !c->dead && glm::length(c->position - m_player.feet()) <= radius)
            {
                hit.push_back(c->id);
            }
        }
        for (const u32 id : hit)
        {
            spellHit(damage, id, "hero");
        }
        break;
    }
    case gameplay::SpellKind::Summon:
    case gameplay::SpellKind::Transform:
        break; // M12 part C2
    }
}

void Engine::fixedUpdateHeroMagic(gameplay::MoveInput& input, f32 seconds)
{
    const bool held = m_castHeld || m_castScripted;
    const bool pressed = held && !m_castWasHeld;
    m_castWasHeld = held;
    if (m_weaponMode != kMagicMode)
    {
        m_heroCast.reset();
        return;
    }
    if (!m_heroCast)
    {
        if (pressed)
        {
            beginHeroCast();
        }
    }
    else if (m_heroCast->charging)
    {
        // Z5: a stage per charge time while held, as far as the mana goes.
        HeroCast& cast = *m_heroCast;
        cast.seconds += seconds;
        const f32 stageSeconds = magicValue(m_scripts.get(), "charge_seconds", 1.0f);
        const u32 wanted = std::min(cast.spell.investStages, static_cast<u32>(cast.seconds / stageSeconds));
        while (cast.stages < wanted &&
               !gameplay::castBlocked(*m_hero, cast.spell, cast.scroll, cast.stages + 1))
        {
            ++cast.stages;
        }
        if (!held)
        {
            releaseHeroCast();
        }
    }
    else if (m_heroCast->cast)
    {
        HeroCast& cast = *m_heroCast;
        cast.seconds += seconds;
        const bool clipOver = cast.state.empty() || !m_figure || m_figure->animator.state() != cast.state;
        // The spell acts at the clip's "cast" event; without a clip after a moment; a clip that ended without
        // the event, or never ends, acts then.
        const bool due = cast.state.empty() ? cast.seconds >= kCastFallbackSeconds
                                            : clipOver || cast.seconds >= kCastMaxSeconds;
        if (!cast.acted && due)
        {
            applyHeroSpell();
        }
        if (m_heroCast && m_heroCast->acted && (clipOver || m_heroCast->seconds >= kCastMaxSeconds))
        {
            const bool usedUp = m_heroCast->scroll && m_hero && m_hero->itemCount(m_heroCast->item) == 0;
            m_heroCast.reset();
            if (usedUp)
            {
                toggleWeapon(); // the last scroll is gone: the hands are empty
            }
        }
    }
    if (m_heroCast)
    {
        input = {}; // the hero stands while charging and casting
    }
}

std::optional<gameplay::SpellInfo> Engine::spellOfItem(std::string_view item) const
{
    const script::Instance* def = m_scripts ? m_scripts->findInstance("Item", item) : nullptr;
    const std::string_view spell = def != nullptr ? def->fields["spell"].asString() : std::string_view();
    const script::Instance* s = spell.empty() ? nullptr : m_scripts->findInstance("Spell", spell);
    return s != nullptr ? std::optional<gameplay::SpellInfo>(gameplay::spellInfo(*s)) : std::nullopt;
}

void Engine::bindMagicFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    vm.bind(
        {"cast_check", "cast_check(item: string, stages?: integer) -> string | nil",
         "Ob der Held die Rune bzw. Spruchrolle wirken kann (M12, Z1-Z3): nil, sonst der Grund (fehlender "
         "Kreis, zu wenig Mana, nicht im Inventar).",
         "Magie", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"expects (item, stages?)"};
             }
             const auto spell = spellOfItem(a[0].asString());
             if (!spell)
             {
                 return Error{std::format("{} is no rune or scroll", a[0].asString())};
             }
             if (!m_hero || m_hero->itemCount(a[0].asString()) == 0)
             {
                 return Value(std::string("Das hast du nicht."));
             }
             const script::Instance* item = m_scripts->findInstance("Item", a[0].asString());
             const bool scroll = item->fields["category"].asString() == "scroll";
             const auto blocked = gameplay::castBlocked(
                 *m_hero, *spell, scroll,
                 a.size() > 1 ? static_cast<u32>(std::max<i64>(0, a[1].asInteger())) : 0u);
             return blocked ? Value(*blocked) : Value();
         }});
    vm.bind(
        {"draw_magic", "draw_magic() -> string",
         "Zieht die Rune bzw. Spruchrolle des zuletzt gewählten Runenplatzes bzw. steckt weg, wie die Taste "
         "draw_magic (M12, Z4); gibt zurück, was danach gezogen ist (das Item, sonst \"none\").",
         "Magie", [this](std::span<const Value>) -> Result<Value>
         {
             toggleMagic();
             return Value(m_weaponMode == kMagicMode ? m_weaponDrawn : std::string("none"));
         }});
    vm.bind(
        {"hero_rune", "hero_rune(place: integer)",
         "Wählt den Runenplatz 1-7 wie die Tasten 4-9 (Z4): zieht ihn bzw. wechselt die Rune in der Hand.",
         "Magie", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isNumber() || a[0].asInteger() < 1 || a[0].asInteger() > 7)
             {
                 return Error{"expects a rune place 1-7"};
             }
             selectRune(static_cast<u32>(a[0].asInteger() - 1));
             return Value();
         }});
    vm.bind({"hero_cast", "hero_cast(hold: boolean)",
             "Hält die Zaubertasten gedrückt (true: aufladen bzw. sofort wirken) bzw. lässt sie los (false: "
             "wirken), wie Strg+vor bzw. die linke Maustaste mit gezogener Magie (Z5).",
             "Magie", [this](std::span<const Value> a) -> Result<Value>
             {
                 m_castScripted = !a.empty() && a[0].asBool();
                 return Value();
             }});
    vm.bind(
        {"hero_casting", "hero_casting() -> string | nil",
         "Der Zauber, den der Held gerade auflädt bzw. wirkt, mit der Aufladestufe (\"spl_firebolt 0\"); nil "
         "ohne.",
         "Magie", [this](std::span<const Value>) -> Result<Value>
         {
             if (!m_heroCast)
             {
                 return Value();
             }
             return Value(std::format("{} {}", m_heroCast->spell.instance, m_heroCast->stages));
         }});
    vm.bind({"npc_cast",
             "on(\"npc_cast\", fn(caster: string, spell: string))",
             "Ein Zauber wird gewirkt (M12, `hero`).",
             "Ereignisse",
             {}});
    vm.bind({"npc_asleep",
             "on(\"npc_asleep\", fn(npc: string, caster: string))",
             "Ein NPC bzw. Tier schläft durch einen Zauber ein (M12, Z6).",
             "Ereignisse",
             {}});
    vm.bind({"npc_woke",
             "on(\"npc_woke\", fn(npc: string))",
             "Ein verzauberter Schläfer wacht auf (Zeit um oder Schaden).",
             "Ereignisse",
             {}});
}
} // namespace g7

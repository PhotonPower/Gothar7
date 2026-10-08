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
    if (m_transform)
    {
        requestTransformBack(); // Z7: "1" makes him human again
        return;
    }
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
    if (const auto sound = spell.sounds.find("cast"); sound != spell.sounds.end())
    {
        (void)playSound(sound->second, hand); // M13
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
        p.burn = spell.burn;
        if (const auto sound = spell.sounds.find("impact"); sound != spell.sounds.end())
        {
            p.impactSound = sound->second;
        }
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
        else if (spell.effect == "fear")
        {
            (void)castFear(target->id, spell.duration > 0.0f ? spell.duration * strength : 10.0f, "hero",
                           fx("on_target"));
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
            spellHit(damage, id, "hero", spell.burn);
        }
        break;
    }
    case gameplay::SpellKind::Summon:
        (void)summonForHero(spell, spell.duration * strength);
        break;
    case gameplay::SpellKind::Transform:
        requestTransform(spell.species); // the transition clip, then the swap
        break;
    }
}

bool Engine::beginTransform(std::string_view species)
{
    if (m_transform || !m_player.valid() || !m_hero || !m_figure || !m_scripts)
    {
        return false;
    }
    // The animal's values: the first Npc of that species (mon_wolf ...).
    const script::Instance* npc = nullptr;
    for (const script::Instance& i : m_scripts->instances())
    {
        if (i.kind == "Npc" && i.fields["species"].asString() == species)
        {
            npc = &i;
            break;
        }
    }
    auto values = npc != nullptr
                      ? gameplay::Character::fromInstance(*npc, itemLookup())
                      : Result<gameplay::Character>(Error{std::format("no Npc of species {}", species)});
    if (!values)
    {
        G7_LOG_WARN("engine", "transformation: {}", values.error().message);
        return false;
    }
    auto figure = loadFigure(std::format("characters/monsters/{0}/rig/{0}_reference.glb", species),
                             std::format("data/anim/{}.animgraph.toml", species));
    if (!figure)
    {
        G7_LOG_WARN("engine", "transformation: {}", figure.error().message);
        return false;
    }
    physics::CharacterDesc desc = creatureBody(species);
    const Vec3 feet = m_player.feet() + Vec3(0.0f, 0.05f, 0.0f);
    auto body = physics::CharacterController::create(m_physics, desc, feet);
    if (!body)
    {
        G7_LOG_WARN("engine", "transformation: {}", body.error().message);
        return false;
    }
    // No weapons, no magic in the paws (Z7).
    m_heroCast.reset();
    m_weaponMode = 0;
    m_weaponDrawn.clear();
    detachFromPlayer("socket_hand_r");
    detachFromPlayer("socket_hand_l");
    m_combatTarget.reset();
    m_heroFighter.reset();

    HeroTransform t;
    t.species = std::string(species);
    t.npc = npc->name;
    t.human = std::move(m_figure);
    t.animal = std::make_unique<gameplay::Character>(std::move(values).value());
    t.humanMovement = m_movementSettings;
    // The camera at the animal's height: data/creatures.toml [<species>] camera_height (m the camera looks
    // at), else scaled by its capsule (human 1.8 m).
    const physics::CharacterDesc human;
    const f32 lookAt =
        m_creatureBodies
            ? static_cast<f32>(m_creatureBodies->get<f64>(std::string(species) + ".camera_height", 0.0))
            : 0.0f;
    t.cameraScale = std::clamp(lookAt > 0.0f ? lookAt / m_movementSettings.camera.targetHeight
                                             : desc.height / human.height,
                               0.3f, 1.5f);
    m_figure = std::move(figure).value();
    if (m_figure->moveRun > 0.0f)
    {
        // Its gaits: walk and run from its clips, sneaking as walking.
        m_movementSettings.walkSpeed = m_figure->moveWalk;
        m_movementSettings.runSpeed = m_figure->moveRun;
        m_movementSettings.sneakSpeed = m_figure->moveWalk;
        m_movementSettings.strafeSpeed = m_figure->moveWalk;
        m_movementSettings.backwardSpeed = m_figure->moveWalk;
    }
    m_player = std::move(body).value();
    m_playerFeet = m_playerFeetBefore = m_player.visualFeet();
    m_transform = std::move(t);
    m_weaponMode = 5; // the animal's fighting keys; the action key does nothing else (no items, no talk)
    resetPlayerAnimation();
    (void)startEffect("summon", feet + Vec3(0.0f, 0.5f, 0.0f));
    G7_LOG_INFO("engine", "the hero becomes a {} ({})", species, m_transform->npc);
    if (m_scripts)
    {
        const script::Value args[] = {std::string(species)};
        m_scripts->emit("hero_transformed", args);
    }
    return true;
}

void Engine::requestTransform(std::string_view species)
{
    if (m_transformOut)
    {
        return;
    }
    if (m_figure && m_figure->animator.hasState("none_t_transform_out"))
    {
        m_figure->animator.enter("none_t_transform_out", 0.15f);
        m_transformOut = TransformOut{std::string(species), "none_t_transform_out", 0.0f};
        return;
    }
    m_transformRequested = std::string(species);
}

void Engine::requestTransformBack()
{
    if (!m_transform || m_transformOut)
    {
        return;
    }
    const std::string out = m_transform->species + "_t_transform_out";
    if (m_figure && m_figure->animator.hasState(out))
    {
        m_figure->animator.enter(out, 0.15f);
        m_transformOut = TransformOut{std::string(), out, 0.0f};
        return;
    }
    m_transformBackRequested = true;
}

void Engine::endTransform()
{
    if (!m_transform)
    {
        return;
    }
    HeroTransform t = std::move(*m_transform);
    m_transform.reset();
    m_movementSettings = t.humanMovement;
    m_figure = std::move(t.human);
    m_weaponMode = 0;
    m_heroFighter.reset();
    m_combatTarget.reset();
    m_heroAnimalAction = 0;
    if (m_player.valid())
    {
        const Vec3 feet = m_player.feet() + Vec3(0.0f, 0.05f, 0.0f);
        physics::CharacterDesc desc;
        desc.maxSlopeDegrees = m_movementSettings.maxSlopeDegrees;
        desc.stepHeight = m_movementSettings.stepHeight;
        desc.stickToFloor = m_movementSettings.stickToFloor;
        if (auto body = physics::CharacterController::create(m_physics, desc, feet))
        {
            m_player = std::move(body).value();
            m_playerFeet = m_playerFeetBefore = m_player.visualFeet();
        }
        (void)startEffect("summon", feet + Vec3(0.0f, 0.8f, 0.0f));
    }
    resetPlayerAnimation();
    G7_LOG_INFO("engine", "the hero is human again");
    if (m_scripts)
    {
        const script::Value args[] = {std::string()};
        m_scripts->emit("hero_transformed", args);
    }
}

bool Engine::castFear(u32 targetId, f32 seconds, std::string_view caster, std::string_view effect)
{
    Creature* c = creature(targetId);
    if (c == nullptr || !c->character || c->dead || c->vanished ||
        c->fighter.state() == gameplay::FightState::Down || c->fighter.state() == gameplay::FightState::Dead)
    {
        return false;
    }
    if (!effect.empty())
    {
        (void)startEffect(effect, c->position + Vec3(0.0f, 1.0f, 0.0f));
    }
    G7_LOG_INFO("engine", "{} is afraid of {} for {:.0f} s", c->species, caster, seconds);
    if (m_scripts)
    {
        const script::Value args[] = {c->species, std::string(caster), static_cast<f64>(seconds)};
        m_scripts->emit("npc_feared", args); // ai/summons.lua: zs_fear
    }
    return true;
}

std::string Engine::npcScrollFor(const gameplay::Character& who, std::string_view spell) const
{
    for (const gameplay::ItemStack& stack : who.inventory(itemLookup()))
    {
        const script::Instance* item = m_scripts ? m_scripts->findInstance("Item", stack.item) : nullptr;
        if (item != nullptr && item->fields["category"].asString() == "scroll" &&
            item->fields["spell"].asString() == spell)
        {
            return stack.item;
        }
    }
    return {};
}

std::optional<std::string> Engine::npcCast(Creature& c, std::string_view spell, std::optional<u32> target)
{
    const script::Instance* def = m_scripts ? m_scripts->findInstance("Spell", spell) : nullptr;
    if (def == nullptr)
    {
        return std::format("no Spell \"{}\"", spell);
    }
    if (!c.character || c.dead || c.vanished || c.cast || c.fighter.state() != gameplay::FightState::Ready)
    {
        return std::string("busy");
    }
    const gameplay::SpellInfo info = gameplay::spellInfo(*def);
    if (info.kind == gameplay::SpellKind::Summon || info.kind == gameplay::SpellKind::Transform)
    {
        return std::string("NPCs do not summon or transform yet");
    }
    // With its circle (a rune it knows), else from a scroll it carries (no circle; used up; decision A).
    const std::string scroll = npcScrollFor(*c.character, info.instance);
    if (const auto blocked = gameplay::castBlocked(*c.character, info, false); blocked)
    {
        if (scroll.empty() || gameplay::castBlocked(*c.character, info, true))
        {
            return *blocked;
        }
        (void)c.character->removeItem(scroll, 1);
    }
    (void)c.character->setAttribute("mana", c.character->attribute("mana") - info.mana);
    Creature::Cast cast;
    cast.spell = info;
    cast.target = target;
    // Facing it; its cast clip (no draw clip: the magic is in the hand at once).
    if (target)
    {
        const Vec3 to =
            (*target == kHeroShooter ? m_player.feet() : creaturePosition(*target).value_or(c.position)) -
            c.position;
        if (glm::length(Vec2(to.x, to.z)) > 0.1f)
        {
            c.yaw = gameplay::yawOf(glm::normalize(Vec3(to.x, 0.0f, to.z)));
        }
    }
    const char* state = castState(info.kind);
    if (c.figure && c.figure->animator.hasState(state))
    {
        c.figure->animator.enter(state, 0.1f);
        cast.state = state;
    }
    c.magicStance = 3.0f;
    c.cast = std::move(cast);
    G7_LOG_INFO("engine", "{} casts {}", c.species, info.instance);
    if (m_scripts)
    {
        const script::Value args[] = {c.species, info.instance};
        m_scripts->emit("npc_cast", args);
    }
    return std::nullopt;
}

void Engine::fixedUpdateNpcCast(Creature& c, f32 seconds)
{
    c.magicStance = std::max(0.0f, c.magicStance - (c.cast ? 0.0f : seconds));
    if (!c.cast)
    {
        return;
    }
    Creature::Cast& cast = *c.cast;
    cast.seconds += seconds;
    const bool clipOver = cast.state.empty() || !c.figure || c.figure->animator.state() != cast.state;
    const bool due = cast.state.empty() ? cast.seconds >= kCastFallbackSeconds
                                        : clipOver || cast.seconds >= kCastMaxSeconds;
    if (!cast.acted && due)
    {
        applyNpcSpell(c);
    }
    if (c.cast && c.cast->acted && (clipOver || c.cast->seconds >= kCastMaxSeconds))
    {
        c.cast.reset();
    }
}

void Engine::applyNpcSpell(Creature& c)
{
    if (!c.cast || c.cast->acted)
    {
        return;
    }
    c.cast->acted = true;
    const gameplay::SpellInfo& spell = c.cast->spell;
    const Vec3 hand = c.position + Vec3(0.0f, 1.4f, 0.0f) + gameplay::forwardOf(c.yaw) * 0.5f;
    const auto fx = [&](std::string_view role) -> std::string
    {
        const auto it = spell.fx.find(role);
        return it != spell.fx.end() ? it->second : std::string();
    };
    if (const std::string cast = fx("cast"); !cast.empty())
    {
        (void)startEffect(cast, hand);
    }
    if (const auto sound = spell.sounds.find("cast"); sound != spell.sounds.end())
    {
        (void)playSound(sound->second, hand); // M13
    }
    const std::optional<u32> target = c.cast->target;
    switch (spell.kind)
    {
    case gameplay::SpellKind::Projectile:
    {
        const std::optional<Vec3> aim = !target ? std::nullopt
                                        : *target == kHeroShooter
                                            ? std::optional<Vec3>(m_player.feet() + Vec3(0.0f, 1.3f, 0.0f))
                                            : aimPoint(*target);
        const Vec3 direction = aim ? glm::normalize(*aim - hand) : gameplay::forwardOf(c.yaw);
        Projectile p;
        p.position = hand;
        p.velocity = direction * magicValue(m_scripts.get(), "projectile_speed", 30.0f);
        p.shooter = c.id;
        p.spell = spell.instance;
        for (const auto& [type, value] : spell.damage)
        {
            p.damage[type] = value;
        }
        if (const std::string trail = fx("trail"); !trail.empty())
        {
            p.trail = startEffect(trail, hand, -direction);
        }
        p.impact = fx("impact");
        p.burn = spell.burn;
        if (const auto sound = spell.sounds.find("impact"); sound != spell.sounds.end())
        {
            p.impactSound = sound->second;
        }
        m_projectiles.push_back(std::move(p));
        break;
    }
    case gameplay::SpellKind::Self:
        (void)c.character->setAttribute(
            "hp", std::min(c.character->attribute("hp_max"), c.character->attribute("hp") + spell.heal));
        if (const std::string on = fx("on_target"); !on.empty())
        {
            (void)startEffect(on, c.position);
        }
        break;
    case gameplay::SpellKind::Target:
        // On NPCs and animals; the hero is not put to sleep or frightened (Gothic 1).
        if (target && *target != kHeroShooter)
        {
            if (spell.effect == "sleep")
            {
                (void)castSleep(*target, spell.duration, c.species, fx("on_target"));
            }
            else if (spell.effect == "fear")
            {
                (void)castFear(*target, spell.duration > 0.0f ? spell.duration : 10.0f, c.species,
                               fx("on_target"));
            }
        }
        break;
    case gameplay::SpellKind::Area:
    {
        std::vector<u32> hit;
        for (const auto& other : m_creatures)
        {
            if (other.get() != &c && other->character && !other->dead &&
                glm::length(other->position - c.position) <= spell.radius)
            {
                hit.push_back(other->id);
            }
        }
        if (m_player.valid() && glm::length(m_player.feet() - c.position) <= spell.radius)
        {
            hit.push_back(kHeroShooter);
        }
        gameplay::DamageByType damage;
        for (const auto& [type, value] : spell.damage)
        {
            damage[type] = value;
        }
        for (const u32 id : hit)
        {
            spellHit(damage, id, c.species, spell.burn);
        }
        break;
    }
    case gameplay::SpellKind::Summon:
    case gameplay::SpellKind::Transform:
        break;
    }
}

void Engine::setBurning(u32 targetId, std::string_view caster)
{
    const bool hero = targetId == kHeroShooter;
    Creature* c = hero ? nullptr : creature(targetId);
    if ((!hero && (c == nullptr || c->dead || c->vanished)) || (hero && !m_player.valid()))
    {
        return;
    }
    Burning& b = m_burning[targetId];
    const bool already = b.seconds > 0.0f;
    b.seconds = magicValue(m_scripts.get(), "burn_seconds", 3.0f);
    b.caster = std::string(caster);
    if (!already)
    {
        b.tick = 1.0f;
        const Vec3 at = (hero ? m_player.feet() : c->position) + Vec3(0.0f, 0.9f, 0.0f);
        b.effect = startEffect("fire", at);
        if (c != nullptr && c->figure && c->figure->animator.hasState("none_s_burn"))
        {
            c->figure->animator.enter("none_s_burn", 0.15f);
        }
        G7_LOG_INFO("engine", "{} burns ({})", hero ? std::string("hero") : c->species, caster);
        if (m_scripts)
        {
            const script::Value args[] = {hero ? std::string("hero") : c->species, std::string(caster)};
            m_scripts->emit("npc_burning", args);
        }
    }
}

void Engine::fixedUpdateBurning(f32 seconds)
{
    if (m_burning.empty())
    {
        return;
    }
    const f32 damagePerTick = magicValue(m_scripts.get(), "burn_damage", 5.0f);
    std::vector<std::pair<u32, std::string>> ticks;
    for (auto it = m_burning.begin(); it != m_burning.end();)
    {
        const u32 id = it->first;
        Burning& b = it->second;
        const bool hero = id == kHeroShooter;
        Creature* c = hero ? nullptr : creature(id);
        bool out = (hero && !m_player.valid()) || (!hero && (c == nullptr || c->dead || c->vanished));
        if (!out)
        {
            const Vec3 feet = hero ? m_player.feet() : c->position;
            // Water puts it out: swimming, or standing in water above the knees.
            const auto surface = m_water.surfaceAt(feet);
            const bool wet = hero ? m_swimmer.mode() != gameplay::WaterMode::Land
                                  : surface.has_value() && *surface > feet.y + 0.4f;
            if (b.effect)
            {
                m_particles.move(*b.effect, feet + Vec3(0.0f, 0.9f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
            }
            // A second's damage at the end of each second (the last one too), unless water put it out.
            b.tick -= seconds;
            b.seconds -= seconds;
            if (!wet && b.tick <= 1e-4f)
            {
                b.tick += 1.0f;
                ticks.emplace_back(id, b.caster);
            }
            out = wet || b.seconds <= 1e-4f;
        }
        if (out)
        {
            if (b.effect)
            {
                m_particles.stop(*b.effect);
            }
            if (c != nullptr && !c->dead && c->figure && c->figure->animator.state() == "none_s_burn")
            {
                c->figure->animator.enter(c->figure->startState, 0.2f); // back on its feet
            }
            it = m_burning.erase(it);
            continue;
        }
        ++it;
    }
    // The fire's damage (after the loop: a death may change the burning ones).
    for (const auto& [id, caster] : ticks)
    {
        spellHit({{"fire", static_cast<i32>(damagePerTick)}}, id, caster, false, false);
    }
}

std::optional<u32> Engine::summonForHero(const gameplay::SpellInfo& spell, f32 seconds)
{
    if (spell.summon.empty() || !m_player.valid())
    {
        return std::nullopt;
    }
    // Z8: one at a time - the one called before goes.
    if (Creature* before = m_heroSummon ? creature(*m_heroSummon) : nullptr;
        before != nullptr && !before->vanished)
    {
        vanish(*before);
    }
    const Vec3 forward = gameplay::forwardOf(m_movement.yaw());
    const Vec3 at = m_player.feet() + forward * 2.0f + Vec3(0.0f, 0.3f, 0.0f);
    auto spawned = spawnNpc(spell.summon, at, m_movement.yaw());
    if (!spawned)
    {
        G7_LOG_WARN("engine", "summon {}: {}", spell.summon, spawned.error().message);
        return std::nullopt;
    }
    Creature& c = *creature(spawned.value());
    c.summoned = true;
    c.summonSeconds = seconds;
    m_heroSummon = c.id;
    const auto fx = spell.fx.find("on_target");
    (void)startEffect(fx != spell.fx.end() ? fx->second : std::string("summon"), at);
    G7_LOG_INFO("engine", "hero summons {} for {:.0f} s", c.species, seconds);
    if (m_scripts)
    {
        const script::Value args[] = {c.species, std::string("hero")};
        m_scripts->emit("npc_summoned", args); // ai/summons.lua: it follows and fights for the hero
    }
    return c.id;
}

void Engine::vanish(Creature& c)
{
    if (c.vanished)
    {
        return;
    }
    (void)startEffect("summon", c.position + Vec3(0.0f, 0.5f, 0.0f));
    if (!c.dead)
    {
        c.fighter.die();
        c.dead = true;
    }
    stopForFight(c);
    c.vanished = true;
    c.body.reset(); // no more collision
    if (c.sleepEffect)
    {
        m_particles.stop(*c.sleepEffect);
        c.sleepEffect.reset();
    }
    if (m_heroSummon == c.id)
    {
        m_heroSummon.reset();
    }
    if (m_combatTarget == c.id)
    {
        m_combatTarget.reset();
    }
    G7_LOG_INFO("engine", "{} vanishes", c.species);
    if (m_scripts)
    {
        const script::Value args[] = {c.species};
        m_scripts->emit("npc_vanished", args);
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
    vm.bind({"hero_target", "hero_target() -> string | nil",
             "Das Ziel, das der Held mit gezogener Waffe bzw. Magie anvisiert (Lock, K5); nil ohne.", "Magie",
             [this](std::span<const Value>) -> Result<Value>
             {
                 const auto target = heroCombatTarget();
                 return target ? Value(*target) : Value();
             }});
    vm.bind({"hero_shape", "hero_shape() -> string | nil",
             "Die Tiergestalt des Helden (Z7: \"wolf\" ...); nil als Mensch.", "Magie",
             [this](std::span<const Value>) -> Result<Value>
             { return m_transform ? Value(m_transform->species) : Value(); }});
    vm.bind({"npc_cast_spell", "npc_cast_spell(npc: string, spell: string, target?: string) -> boolean",
             "Ein NPC wirkt einen Spruch (M12 Teil D) mit seinem Mana und Kreis (Talent magic_circle): auf "
             "`target` (\"hero\" oder ein NPC), ohne Ziel auf sich selbst; false, wenn er es nicht kann.",
             "Magie", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.size() < 2 || !a[0].isString() || !a[1].isString())
                 {
                     return Error{"expects (npc, spell, target?)"};
                 }
                 const auto id = npcByInstance(a[0].asString());
                 if (!id)
                 {
                     return Error{std::format("no {} in the world", a[0].asString())};
                 }
                 std::optional<u32> target;
                 if (a.size() > 2 && a[2].isString())
                 {
                     target = a[2].asString() == "hero" ? std::optional<u32>(kHeroShooter)
                                                        : npcByInstance(a[2].asString());
                     if (!target)
                     {
                         return Error{std::format("no {} in the world", a[2].asString())};
                     }
                 }
                 const auto why = npcCast(*creature(*id), a[1].asString(), target);
                 if (why)
                 {
                     G7_LOG_DEBUG("engine", "{} cannot cast {}: {}", a[0].asString(), a[1].asString(), *why);
                 }
                 return Value(!why.has_value());
             }});
    vm.bind({"npc_can_cast", "npc_can_cast(npc: string, spell: string) -> boolean",
             "Ob ein NPC den Spruch jetzt wirken könnte (Kreis, Mana, nicht beschäftigt).", "Magie",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.size() < 2 || !a[0].isString() || !a[1].isString())
                 {
                     return Error{"expects (npc, spell)"};
                 }
                 const auto id = npcByInstance(a[0].asString());
                 const Creature* c = id ? creature(*id) : nullptr;
                 const script::Instance* def = m_scripts->findInstance("Spell", a[1].asString());
                 if (c == nullptr || def == nullptr || !c->character || c->cast)
                 {
                     return Value(false);
                 }
                 const gameplay::SpellInfo info = gameplay::spellInfo(*def);
                 const bool scroll = !npcScrollFor(*c->character, info.instance).empty();
                 return Value(!gameplay::castBlocked(*c->character, info, false).has_value() ||
                              (scroll && !gameplay::castBlocked(*c->character, info, true).has_value()));
             }});
    vm.bind({"npc_casting", "npc_casting(npc: string) -> boolean", "Ob ein NPC gerade einen Spruch wirkt.",
             "Magie", [this](std::span<const Value> a) -> Result<Value>
             {
                 const auto id =
                     !a.empty() && a[0].isString() ? npcByInstance(a[0].asString()) : std::nullopt;
                 const Creature* c = id ? creature(*id) : nullptr;
                 return Value(c != nullptr && c->cast.has_value());
             }});
    vm.bind({"npc_burning",
             "on(\"npc_burning\", fn(npc: string, caster: string))",
             "Ein NPC, ein Tier bzw. der Held (`hero`) gerät in Brand (M12, Entscheidung B: nur Sprüche mit "
             "burn).",
             "Ereignisse",
             {}});
    vm.bind({"npc_feared",
             "on(\"npc_feared\", fn(npc: string, caster: string, seconds: number))",
             "Ein Furcht-Zauber trifft einen NPC bzw. ein Tier (M12, Z6): er flieht `seconds` Sekunden.",
             "Ereignisse",
             {}});
    vm.bind({"hero_transform_back", "hero_transform_back()",
             "Der Held wird wieder Mensch (wie die Taste „1“ in Tiergestalt).", "Magie",
             [this](std::span<const Value>) -> Result<Value>
             {
                 if (m_transform)
                 {
                     requestTransformBack();
                 }
                 return Value();
             }});
    vm.bind({"hero_transformed",
             "on(\"hero_transformed\", fn(species: string))",
             "Der Held nimmt eine Tiergestalt an (M12, Z7) bzw. wird wieder Mensch (`species` leer).",
             "Ereignisse",
             {}});
    vm.bind({"hero_summon", "hero_summon() -> string | nil",
             "Das vom Helden beschworene Wesen, solange es da ist (Z8); nil ohne.", "Magie",
             [this](std::span<const Value>) -> Result<Value>
             {
                 const Creature* c = m_heroSummon ? creature(*m_heroSummon) : nullptr;
                 return c != nullptr && !c->vanished ? Value(c->species) : Value();
             }});
    vm.bind({"npc_summoned",
             "on(\"npc_summoned\", fn(npc: string, caster: string))",
             "Ein Wesen wurde beschworen (M12, Z8); es verschwindet nach seiner Zeit bzw. nach seinem Tod.",
             "Ereignisse",
             {}});
    vm.bind({"npc_vanished",
             "on(\"npc_vanished\", fn(npc: string))",
             "Ein beschworenes Wesen verschwindet (Zeit um, tot, ein neues beschworen).",
             "Ereignisse",
             {}});
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

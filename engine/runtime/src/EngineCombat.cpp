// Combat (M11 part A, docs/modules/gameplay.md "Kampf"): fighters for the hero and every NPC
// (gameplay::Fighter), hits within reach and angle during an attack's hit window - once per swing and target
// -, the parry (K6), the damage (K2, K3), and what a hit does: stagger, knocked out or dead (K7); the hero
// gets up again at the spot with little life (K8). Values in data/combat.lua. Until the human combat clips
// arrive (figuren) the moves follow the fighter's fallback timeline.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Mobs.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/CombatInput.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace g7
{
namespace
{
using gameplay::AttackKind;
using gameplay::FightState;
using script::Value;

constexpr u32 kHeroId = ~u32(0);
constexpr f32 kBodyRadius = 0.35f;     ///< reach counts from the body's front, to the target's
constexpr f32 kHeroDownSeconds = 5.0f; ///< K8: the hero lies this long, then gets up
constexpr f32 kHeroUpLife = 0.1f;      ///< ... with this share of his life

std::string_view stateName(FightState s) noexcept
{
    switch (s)
    {
    case FightState::Ready:
        return "ready";
    case FightState::Attack:
        return "attack";
    case FightState::Parry:
        return "parry";
    case FightState::Dodge:
        return "dodge";
    case FightState::Stagger:
        return "stagger";
    case FightState::Down:
        return "down";
    case FightState::Dead:
        return "dead";
    }
    return "ready";
}

std::optional<AttackKind> kindOf(std::span<const Value> a, usize index)
{
    if (a.size() <= index || a[index].isNil())
    {
        return AttackKind::Front;
    }
    const std::string_view k = a[index].asString();
    if (k == "front")
    {
        return AttackKind::Front;
    }
    if (k == "left")
    {
        return AttackKind::Left;
    }
    if (k == "right")
    {
        return AttackKind::Right;
    }
    return std::nullopt;
}
} // namespace

/// Somebody fighting: the hero or an NPC, as the combat code needs them.
struct Engine::Combatant
{
    u32 id = 0; ///< kHeroId for the hero
    Creature* creature = nullptr;
    gameplay::Character* character = nullptr;
    gameplay::Fighter* fighter = nullptr;
    std::vector<u32>* hitThisSwing = nullptr;
    Vec3 position{0.0f};
    f32 yaw = 0.0f;
    std::string name; ///< Npc instance, "hero"
    bool animal = false;
};

std::optional<Engine::Combatant> Engine::combatant(u32 id)
{
    Combatant c;
    c.id = id;
    if (id == kHeroId)
    {
        if (!m_player.valid() || !m_hero)
        {
            return std::nullopt;
        }
        c.character = m_hero.get();
        c.fighter = &m_heroFighter;
        c.hitThisSwing = &m_heroHitThisSwing;
        c.position = m_player.feet();
        c.yaw = m_movement.yaw();
        c.name = "hero";
        return c;
    }
    Creature* creature = this->creature(id);
    if (creature == nullptr || !creature->character)
    {
        return std::nullopt;
    }
    c.creature = creature;
    c.character = creature->character.get();
    c.fighter = &creature->fighter;
    c.hitThisSwing = &creature->hitThisSwing;
    c.position = creature->position;
    c.yaw = creature->yaw;
    c.name = creature->species;
    c.animal = m_scripts && m_scripts->findInstance("Npc", creature->species) != nullptr &&
               !m_scripts->findInstance("Npc", creature->species)->fields["species"].asString().empty();
    return c;
}

void Engine::loadCombat()
{
    const script::Value table = m_scripts ? m_scripts->global("Combat") : script::Value();
    if (table.asTable() == nullptr)
    {
        G7_LOG_WARN("engine", "data/combat.lua: no Combat table (default combat values)");
        return;
    }
    auto settings = gameplay::CombatSettings::fromTable(*table.asTable());
    if (!settings)
    {
        G7_LOG_WARN("engine", "data/combat.lua: {} (default combat values)", settings.error().message);
        return;
    }
    m_combat = settings.value();
}

std::string Engine::meleeWeapon(const Combatant& c) const
{
    if (c.id == kHeroId)
    {
        return m_weaponMode == 1 ? m_weaponDrawn : std::string();
    }
    return c.animal ? std::string() : c.character->equipped(gameplay::EquipSlot::Melee);
}

std::string Engine::fightMode(const Combatant& c) const
{
    const std::string weapon = meleeWeapon(c);
    const script::Instance* item =
        weapon.empty() || !m_scripts ? nullptr : m_scripts->findInstance("Item", weapon);
    if (item != nullptr && item->fields["category"].asString() == "melee_2h")
    {
        return "2h";
    }
    return item != nullptr ? "1h" : "fist";
}

f32 Engine::reachOf(const Combatant& c) const
{
    const std::string mode = fightMode(c);
    return mode == "2h" ? m_combat.reach2h : mode == "1h" ? m_combat.reach1h : m_combat.fistReach;
}

bool Engine::startFight(Combatant& c, std::string_view move, AttackKind kind)
{
    const std::string mode = fightMode(c);
    const i32 talent = c.character->talent(mode == "2h" ? "melee_2h" : "melee_1h");
    bool started = false;
    if (move == "attack")
    {
        started = c.fighter->attack(kind, talent, m_combat);
    }
    else if (move == "parry")
    {
        started = c.fighter->parry();
    }
    else if (move == "dodge")
    {
        started = c.fighter->dodge();
    }
    if (!started)
    {
        return false;
    }
    // The human combat clips are not there yet: the fighter's timeline; animals show their attack clip.
    c.fighter->useTimeline();
    if (c.creature != nullptr && c.animal && move == "attack")
    {
        c.creature->action = c.fighter->comboHit() % 2 == 1 ? 1 : 2;
    }
    return true;
}

void Engine::fixedUpdateCombat(f32 seconds)
{
    std::vector<u32> ids;
    if (m_player.valid() && m_hero)
    {
        ids.push_back(kHeroId);
    }
    for (const auto& c : m_creatures)
    {
        if (c->character)
        {
            ids.push_back(c->id);
        }
    }
    for (const u32 id : ids)
    {
        auto attacker = combatant(id);
        if (!attacker)
        {
            continue;
        }
        const FightState before = attacker->fighter->state();
        attacker->fighter->update(seconds, m_combat);
        if (attacker->fighter->takeNewSwing())
        {
            attacker->hitThisSwing->clear();
        }
        if (id == kHeroId && before == FightState::Down && attacker->fighter->state() == FightState::Ready)
        {
            // K8: up again at the spot, with little life.
            const i32 max = m_hero->attribute("hp_max");
            (void)m_hero->setAttribute("hp", std::max(1, static_cast<i32>(std::lround(max * kHeroUpLife))));
        }
        if (!attacker->fighter->hitWindow())
        {
            continue;
        }
        const f32 reach = reachOf(*attacker);
        const Vec3 forward = gameplay::forwardOf(attacker->yaw);
        for (const u32 other : ids)
        {
            if (other == id || std::find(attacker->hitThisSwing->begin(), attacker->hitThisSwing->end(),
                                         other) != attacker->hitThisSwing->end())
            {
                continue;
            }
            auto target = combatant(other);
            if (!target || target->fighter->state() == FightState::Dead)
            {
                continue;
            }
            // A pack or comrades of one guild do not hit each other (Gothic); the hero hits anyone.
            if (id != kHeroId && other != kHeroId && !attacker->character->guild().empty() &&
                attacker->character->guild() == target->character->guild())
            {
                continue;
            }
            const Vec3 to = target->position - attacker->position;
            const f32 distance = glm::length(Vec2(to.x, to.z));
            if (std::abs(to.y) > 1.5f || distance - 2.0f * kBodyRadius > reach)
            {
                continue;
            }
            const f32 cosine =
                distance > 1e-3f ? glm::dot(Vec2(to.x, to.z) / distance, Vec2(forward.x, forward.z)) : 1.0f;
            if (cosine < std::cos(glm::radians(m_combat.hitAngleDegrees)))
            {
                continue;
            }
            attacker->hitThisSwing->push_back(other);
            resolveHit(*attacker, *target);
        }
    }
}

void Engine::resolveHit(Combatant& attacker, Combatant& target)
{
    const auto emit = [&](const char* event, std::initializer_list<Value> args)
    {
        if (m_scripts)
        {
            const std::vector<Value> values(args);
            m_scripts->emit(event, values);
        }
    };
    // K6: a parry from the front blocks - not against animals, and fists do not block weapons.
    const bool weaponVsFists = !meleeWeapon(attacker).empty() && meleeWeapon(target).empty();
    if (target.fighter->parrying(m_combat) && !attacker.animal && !weaponVsFists &&
        gameplay::facesAttacker(target.yaw, Vec2(target.position.x, target.position.z),
                                Vec2(attacker.position.x, attacker.position.z), m_combat.parryAngleDegrees))
    {
        attacker.fighter->stagger(); // the blow bounces off
        attacker.fighter->useTimeline();
        emit("npc_parried", {target.name, attacker.name});
        return;
    }

    // K2, K3: weapon (or the animal's own damage, or fists) + strength - protection.
    gameplay::DamageByType weapon;
    const std::string item = meleeWeapon(attacker);
    const script::Instance* def =
        !m_scripts     ? nullptr
        : item.empty() ? m_scripts->findInstance("Npc", attacker.id == kHeroId ? "pc_hero" : attacker.name)
                       : m_scripts->findInstance("Item", item);
    if (def != nullptr && (attacker.animal || !item.empty()))
    {
        if (const script::Table* t = def->fields["damage"].asTable())
        {
            for (const auto& [type, value] : t->fields)
            {
                weapon[type] = static_cast<i32>(value.asInteger());
            }
        }
    }
    const std::string mode = fightMode(attacker);
    const i32 talent =
        attacker.animal ? 0 : attacker.character->talent(mode == "2h" ? "melee_2h" : "melee_1h");
    const f32 roll = m_random ? m_random() : std::uniform_real_distribution<f32>(0.0f, 1.0f)(m_rng);
    const gameplay::DamageResult hit = gameplay::meleeDamage(
        weapon, attacker.character->attribute("str"),
        [&](std::string_view type) { return target.character->protection(type); }, talent, roll, m_combat);

    const i32 hp = target.character->attribute("hp") - hit.damage;
    (void)target.character->setAttribute("hp", std::max(hp, 0));
    emit("npc_hit", {attacker.name, target.name, Value(static_cast<i64>(hit.damage)), Value(hit.critical)});
    G7_LOG_INFO("engine", "{} hits {}: {} damage{} ({} left)", attacker.name, target.name, hit.damage,
                hit.critical ? " (critical)" : "", std::max(hp, 0));
    if (hp > 0)
    {
        target.fighter->stagger();
        target.fighter->useTimeline();
        if (target.creature != nullptr && target.animal)
        {
            target.creature->action = 3; // the hit clip
        }
        return;
    }
    // K7: people beaten in melee by people fall unconscious; a blow on the one lying, animals and the
    // hero's foes die. K8: the hero is never killed - he gets up again at the spot.
    if (target.id == kHeroId)
    {
        (void)target.character->setAttribute("hp", 1);
        target.fighter->knockOut(kHeroDownSeconds);
        emit("npc_knocked_out", {target.name, attacker.name});
        return;
    }
    const bool knockOut = !attacker.animal && !target.animal && target.fighter->state() != FightState::Down;
    if (knockOut)
    {
        (void)target.character->setAttribute("hp", 1);
        target.fighter->knockOut(m_combat.knockoutSeconds);
        stopForFight(*target.creature);
        emit("npc_knocked_out", {target.name, attacker.name});
        return;
    }
    target.fighter->die();
    target.creature->dead = true;
    stopForFight(*target.creature);
    emit("npc_killed", {target.name, attacker.name});
}

void Engine::stopForFight(Creature& c)
{
    c.commands.clear();
    c.commandRunning = false;
    c.route.reset();
    c.speed = 0.0f;
}

void Engine::bindCombatFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    const auto npcMove = [this](std::string_view move)
    {
        return [this, move = std::string(move)](std::span<const Value> a) -> Result<Value>
        {
            if (a.empty() || !a[0].isString())
            {
                return Error{"argument 1 must be an NPC"};
            }
            const auto id = npcByInstance(a[0].asString());
            auto c = id ? combatant(*id) : std::nullopt;
            if (!c)
            {
                return Error{std::format("no NPC {} in the world", a[0].asString())};
            }
            const auto kind = kindOf(a, 1);
            if (!kind)
            {
                return Error{"the attack must be \"front\", \"left\" or \"right\""};
            }
            return Value(startFight(*c, move, *kind));
        };
    };
    vm.bind({"npc_attack", "npc_attack(npc: string, kind?: \"front\"|\"left\"|\"right\") -> boolean",
             "Ein Schlag (M11): `front` setzt die Kombo fort, soweit das Talent reicht; false, wenn er "
             "gerade nicht "
             "kann.",
             "Kampf", npcMove("attack")});
    vm.bind({"npc_parry", "npc_parry(npc: string) -> boolean",
             "Parade (M11, K6): blockt Nahkampftreffer von vorn kurz nach ihrem Beginn.", "Kampf",
             npcMove("parry")});
    vm.bind({"npc_dodge", "npc_dodge(npc: string) -> boolean", "Ausweichschritt zurück (M11).", "Kampf",
             npcMove("dodge")});
    const auto heroMove = [this](std::string_view move)
    {
        return [this, move = std::string(move)](std::span<const Value> a) -> Result<Value>
        {
            auto c = combatant(kHeroId);
            if (!c)
            {
                return Error{"no hero"};
            }
            const auto kind = kindOf(a, 0);
            if (!kind)
            {
                return Error{"the attack must be \"front\", \"left\" or \"right\""};
            }
            return Value(startFight(*c, move, *kind));
        };
    };
    vm.bind({"npc_stat", "npc_stat(npc: string, name: string) -> integer",
             "Ein Attribut eines NPCs (hp, hp_max, str, dex ...).", "NPCs",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 const auto id = a.size() >= 2 && a[0].isString() && a[1].isString()
                                     ? npcByInstance(a[0].asString())
                                     : std::nullopt;
                 auto c = id ? combatant(*id) : std::nullopt;
                 if (!c)
                 {
                     return Error{"expects (npc, name) of an NPC in the world"};
                 }
                 return Value(static_cast<i64>(c->character->attribute(a[1].asString())));
             }});
    vm.bind({"npc_set_stat", "npc_set_stat(npc: string, name: string, value: integer)",
             "Setzt ein Attribut eines NPCs; `hp`/`mana` bleiben zwischen 0 und dem Maximum.", "NPCs",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 const auto id = a.size() >= 3 && a[0].isString() && a[1].isString() && a[2].isNumber()
                                     ? npcByInstance(a[0].asString())
                                     : std::nullopt;
                 auto c = id ? combatant(*id) : std::nullopt;
                 if (!c)
                 {
                     return Error{"expects (npc, name, value) of an NPC in the world"};
                 }
                 if (auto ok =
                         c->character->setAttribute(a[1].asString(), static_cast<i32>(a[2].asInteger()));
                     !ok)
                 {
                     return ok.error();
                 }
                 return Value();
             }});
    vm.bind(
        {"npc_teleport", "npc_teleport(npc: string, x: number, y: number, z: number, yaw?: number)",
         "Setzt ein NPC sofort an einen Ort (Meter) und dreht es (Grad, 0 = Blick nach -Z, positiv links).",
         "NPCs", [this](std::span<const Value> a) -> Result<Value>
         {
             const auto id =
                 a.size() >= 4 && a[0].isString() && a[1].isNumber() && a[2].isNumber() && a[3].isNumber()
                     ? npcByInstance(a[0].asString())
                     : std::nullopt;
             Creature* c = id ? creature(*id) : nullptr;
             if (c == nullptr)
             {
                 return Error{"expects (npc, x, y, z, yaw?) of an NPC in the world"};
             }
             const Vec3 p(static_cast<f32>(a[1].asNumber()), static_cast<f32>(a[2].asNumber()),
                          static_cast<f32>(a[3].asNumber()));
             c->route.reset();
             if (c->body)
             {
                 c->body->teleport(p);
                 c->position = c->positionBefore = c->body->feet();
             }
             else
             {
                 c->position = c->positionBefore = p;
             }
             if (a.size() > 4 && a[4].isNumber())
             {
                 c->yaw = glm::radians(static_cast<f32>(a[4].asNumber()));
             }
             return Value();
         }});
    // Who a fighter is: an NPC or "hero".
    const auto fighterOf = [this](const Value& v) -> std::optional<Combatant>
    {
        if (!v.isString())
        {
            return std::nullopt;
        }
        const std::optional<u32> id =
            v.asString() == "hero" ? std::optional<u32>(kHeroId) : npcByInstance(v.asString());
        return id ? combatant(*id) : std::nullopt;
    };
    vm.bind({"npc_distance", "npc_distance(npc: string, other: string) -> number",
             "Abstand zweier Kämpfer auf dem Boden in Metern (`hero` für den Helden).", "Kampf",
             [fighterOf](std::span<const Value> a) -> Result<Value>
             {
                 auto x = a.size() >= 2 ? fighterOf(a[0]) : std::nullopt;
                 auto y = a.size() >= 2 ? fighterOf(a[1]) : std::nullopt;
                 if (!x || !y)
                 {
                     return Error{"expects (npc, other) in the world (\"hero\" for the hero)"};
                 }
                 const Vec3 d = y->position - x->position;
                 return Value(static_cast<f64>(glm::length(Vec2(d.x, d.z))));
             }});
    vm.bind({"npc_face", "npc_face(npc: string, other: string)",
             "Dreht ein NPC sofort zu einem anderen bzw. zum Helden (Kampf).", "Kampf",
             [this, fighterOf](std::span<const Value> a) -> Result<Value>
             {
                 auto x = a.size() >= 2 ? fighterOf(a[0]) : std::nullopt;
                 auto y = a.size() >= 2 ? fighterOf(a[1]) : std::nullopt;
                 if (!x || !y || x->creature == nullptr)
                 {
                     return Error{"expects (npc, other) in the world"};
                 }
                 const Vec3 d = y->position - x->position;
                 if (glm::length(Vec2(d.x, d.z)) > 1e-3f)
                 {
                     x->creature->yaw = gameplay::yawOf(glm::normalize(Vec3(d.x, 0.0f, d.z)));
                 }
                 return Value();
             }});
    vm.bind({"npc_reach", "npc_reach(npc: string) -> number",
             "Bis zu welchem Abstand (Mitte zu Mitte, m) die Schläge des Kämpfers treffen (`hero` für den "
             "Helden).",
             "Kampf", [this, fighterOf](std::span<const Value> a) -> Result<Value>
             {
                 auto x = !a.empty() ? fighterOf(a[0]) : std::nullopt;
                 if (!x)
                 {
                     return Error{"expects an NPC in the world"};
                 }
                 return Value(static_cast<f64>(reachOf(*x) + 2.0f * kBodyRadius));
             }});
    vm.bind({"hero_attack", "hero_attack(kind?: \"front\"|\"left\"|\"right\") -> boolean",
             "Ein Schlag des Helden (sonst über die Steuerung).", "Kampf", heroMove("attack")});
    vm.bind({"hero_parry", "hero_parry() -> boolean", "Parade des Helden.", "Kampf", heroMove("parry")});
    vm.bind({"fight_state", "fight_state(npc: string) -> string",
             "Kampfzustand: ready, attack, parry, dodge, stagger, down (bewusstlos), dead; `hero` für den "
             "Helden.",
             "Kampf", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"argument 1 must be an NPC or \"hero\""};
                 }
                 const std::optional<u32> id =
                     a[0].asString() == "hero" ? std::optional<u32>(kHeroId) : npcByInstance(a[0].asString());
                 auto c = id ? combatant(*id) : std::nullopt;
                 if (!c)
                 {
                     return Error{std::format("no {} in the world", a[0].asString())};
                 }
                 return Value(std::string(stateName(c->fighter->state())));
             }});
    for (const auto& [name, signature, text] :
         {std::tuple{"npc_hit",
                     "on(\"npc_hit\", fn(attacker: string, target: string, damage: number, critical: "
                     "boolean))",
                     "Ein Nahkampftreffer (M11; `hero` für den Helden)."},
          std::tuple{"npc_parried", "on(\"npc_parried\", fn(defender: string, attacker: string))",
                     "Ein Schlag wurde pariert (M11)."},
          std::tuple{"npc_knocked_out", "on(\"npc_knocked_out\", fn(target: string, attacker: string))",
                     "Bewusstlos geschlagen (M11, K7); auch der Held (K8: er steht am Ort wieder auf)."},
          std::tuple{"npc_killed", "on(\"npc_killed\", fn(target: string, attacker: string))",
                     "Getötet (M11, K7)."}})
    {
        vm.bind({name, signature, text, "Ereignisse", {}});
    }
}
} // namespace g7

namespace g7
{
namespace
{
constexpr u64 kCreatureFocusBit = 1ull << 62; ///< as in Engine.cpp: the focus is a creature
constexpr f32 kLockRange = 8.0f;              ///< K5: an enemy this near (and ahead) is locked when drawing
constexpr f32 kLockKeepRange = 12.0f;         ///< ... and kept until farther
constexpr f32 kRangedLockRange = 30.0f;       ///< with a bow or crossbow drawn (R2)
constexpr f32 kLockTurnRate = 6.0f;           ///< rad/s: the hero turns to the locked enemy
constexpr f32 kDodgeBackwardInput = -1.0f;    ///< until the dodge clip (root motion): a quick step back
} // namespace

void Engine::readCombatInput()
{
    const runtime::CombatInput asked = runtime::combatInput(m_actions, m_input);
    if (asked.move)
    {
        m_combatRequest = CombatRequest{asked.move->first, asked.move->second};
    }
    if (asked.holdsAction)
    {
        m_playerInput.forward = 0.0f;
        m_playerInput.strafe = 0.0f;
        m_playerInput.turn = 0.0f;
        m_playerInput.jump = false;
    }
}

std::optional<u32> Engine::pickCombatTarget() const
{
    // K5 (Gothic 1): the nearest living NPC ahead within reach of the lock - the focused one first.
    if (!m_player.valid())
    {
        return std::nullopt;
    }
    const Vec3 hero = m_player.feet();
    const Vec3 forward = gameplay::forwardOf(m_movement.yaw());
    std::optional<u32> best;
    const f32 range = m_weaponMode == 3 ? kRangedLockRange : kLockRange;
    f32 bestDistance = range;
    for (const auto& c : m_creatures)
    {
        if (!c->character || c->fighter.state() == FightState::Dead || c->fighter.state() == FightState::Down)
        {
            continue;
        }
        const Vec3 to = c->position - hero;
        const f32 distance = glm::length(Vec2(to.x, to.z));
        if (distance > range ||
            (distance > 0.5f && glm::dot(Vec2(to.x, to.z) / distance, Vec2(forward.x, forward.z)) < 0.0f))
        {
            continue;
        }
        const bool focused = m_focus && m_focus->kind == gameplay::FocusKind::Npc &&
                             static_cast<u32>(m_focus->id & ~kCreatureFocusBit) == c->id;
        const f32 score = focused ? 0.0f : distance;
        if (!best || score < bestDistance)
        {
            best = c->id;
            bestDistance = score;
        }
    }
    return best;
}

void Engine::fixedUpdateHeroFight(gameplay::MoveInput& input, f32 seconds)
{
    if (m_combatRequest)
    {
        if (m_weaponMode == 3)
        {
            if (m_combatRequest->move == "attack")
            {
                (void)shootRanged(); // R2: the fighting keys shoot with a bow drawn
            }
        }
        else if (auto hero = combatant(kHeroId); hero && m_weaponMode != 0)
        {
            (void)startFight(*hero, m_combatRequest->move, m_combatRequest->kind);
        }
        m_combatRequest.reset();
    }
    // K5: with the weapon drawn the hero keeps the enemy in view - turning to it, the turn keys strafe.
    if (m_weaponMode == 0)
    {
        m_combatTarget.reset();
    }
    else
    {
        const Creature* target = m_combatTarget ? creature(*m_combatTarget) : nullptr;
        if (target == nullptr || target->fighter.state() == FightState::Dead ||
            target->fighter.state() == FightState::Down ||
            glm::length(
                Vec2(target->position.x - m_player.feet().x, target->position.z - m_player.feet().z)) >
                (m_weaponMode == 3 ? kRangedLockRange + 5.0f : kLockKeepRange))
        {
            m_combatTarget = pickCombatTarget();
            target = m_combatTarget ? creature(*m_combatTarget) : nullptr;
        }
        if (target != nullptr)
        {
            const Vec3 to = target->position - m_player.feet();
            if (glm::length(Vec2(to.x, to.z)) > 0.1f)
            {
                const f32 wanted = gameplay::yawOf(glm::normalize(Vec3(to.x, 0.0f, to.z)));
                const f32 turn = std::remainder(wanted - m_movement.yaw(), 2.0f * glm::pi<f32>());
                m_movement.setYaw(m_movement.yaw() +
                                  std::clamp(turn, -kLockTurnRate * seconds, kLockTurnRate * seconds));
            }
            input.strafe = std::clamp(input.strafe + input.turn, -1.0f, 1.0f);
            input.turn = 0.0f;
            input.mouseTurn = 0.0f;
        }
    }
    // The hero stands while striking, parrying, staggering or lying; a dodge is a quick step back.
    switch (m_heroFighter.state())
    {
    case FightState::Ready:
        break;
    case FightState::Dodge:
        input = {};
        input.forward = kDodgeBackwardInput;
        break;
    default:
        input = {};
        break;
    }
}

std::optional<std::string> Engine::heroCombatTarget() const
{
    const Creature* c = m_combatTarget ? creature(*m_combatTarget) : nullptr;
    return c != nullptr ? std::optional<std::string>(c->species) : std::nullopt;
}
} // namespace g7

namespace g7::runtime
{
CombatInput combatInput(const platform::ActionMap& actions, const platform::Input& input)
{
    using platform::Action;
    const auto pressed = [&](Action a) { return actions.pressed(input, a); };
    const auto down = [&](Action a) { return actions.isDown(input, a); };
    CombatInput result;
    if (down(Action::Action))
    {
        result.holdsAction = true;
        if (pressed(Action::MoveForward))
        {
            result.move = {"attack", AttackKind::Front};
        }
        else if (pressed(Action::TurnLeft) || pressed(Action::StrafeLeft))
        {
            result.move = {"attack", AttackKind::Left};
        }
        else if (pressed(Action::TurnRight) || pressed(Action::StrafeRight))
        {
            result.move = {"attack", AttackKind::Right};
        }
        else if (pressed(Action::MoveBack))
        {
            result.move = {"parry", AttackKind::Front};
        }
        else if (pressed(Action::Jump))
        {
            result.move = {"dodge", AttackKind::Front};
        }
        return result;
    }
    if (pressed(Action::Attack))
    {
        const bool left = down(Action::TurnLeft) || down(Action::StrafeLeft);
        const bool right = down(Action::TurnRight) || down(Action::StrafeRight);
        result.move = {"attack", left ? AttackKind::Left : right ? AttackKind::Right : AttackKind::Front};
    }
    else if (pressed(Action::Parry))
    {
        result.move = {"parry", AttackKind::Front};
    }
    return result;
}
} // namespace g7::runtime

namespace g7
{
namespace
{
constexpr f32 kLootReach = 3.0f; ///< m: the hero takes from a lying NPC this near
} // namespace

bool Engine::focusedNpcLying() const
{
    if (!m_focus || m_focus->kind != gameplay::FocusKind::Npc)
    {
        return false;
    }
    const Creature* c = creature(static_cast<u32>(m_focus->id & ~kCreatureFocusBit));
    return c != nullptr && (c->fighter.state() == FightState::Down || c->fighter.state() == FightState::Dead);
}

Result<void> Engine::lootFocus()
{
    if (!focusedNpcLying())
    {
        return Error{"nobody lying in focus"};
    }
    m_lootTarget = static_cast<u32>(m_focus->id & ~kCreatureFocusBit);
    m_inventoryMessage.clear();
    setInventoryOpen(true);
    return {};
}

Result<u32> Engine::loot(std::string_view npc, std::string_view item, u32 count)
{
    const auto id = npcByInstance(npc);
    Creature* c = id ? creature(*id) : nullptr;
    if (c == nullptr || !c->character || !m_hero || !m_player.valid())
    {
        return Error{std::format("no NPC {} in the world", npc)};
    }
    if (c->fighter.state() != FightState::Down && c->fighter.state() != FightState::Dead)
    {
        return Error{"Nur Bewusstlose und Tote lassen sich plündern."};
    }
    if (glm::length(c->position - m_player.feet()) > kLootReach)
    {
        return Error{"Zu weit weg."};
    }
    const u32 have = c->character->itemCount(item);
    const u32 take = count == 0 ? have : std::min(count, have);
    if (take == 0 || !c->character->removeItem(item, take))
    {
        return Error{std::format("{} hat kein {}.", c->character->name(), item)};
    }
    m_hero->addItem(item, take);
    if (m_scripts)
    {
        const script::Value args[] = {std::string(npc), std::string(item), Value(static_cast<i64>(take))};
        m_scripts->emit("npc_looted", args);
    }
    return take;
}

void Engine::bindLootFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind(
        {"loot", "loot(npc: string, item: string, count?: integer) -> integer",
         "Nimmt einem bewusstlosen oder toten NPC in der Nähe des Helden `count` (ohne: alle) Stück ab (M11, "
         "K7); gibt die Zahl zurück.",
         "Kampf", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.size() < 2 || !a[0].isString() || !a[1].isString())
             {
                 return Error{"expects (npc, item, count?)"};
             }
             auto taken = loot(a[0].asString(), a[1].asString(),
                               a.size() > 2 ? static_cast<u32>(std::max<i64>(a[2].asInteger(), 0)) : 0u);
             if (!taken)
             {
                 return taken.error();
             }
             return Value(static_cast<i64>(taken.value()));
         }});
    vm.bind({"npc_looted",
             "on(\"npc_looted\", fn(npc: string, item: string, count: integer))",
             "Der Held hat einem Bewusstlosen oder Toten etwas abgenommen (M11).",
             "Ereignisse",
             {}});
}
} // namespace g7

namespace g7
{
/// A projectile hit (M11 part E, EngineRanged.cpp): R3 damage, the arrow stays in the target, K7 ranged
/// kills.
void Engine::projectileHit(const Projectile& p, u32 targetId)
{
    auto target = combatant(targetId);
    if (!target)
    {
        return;
    }
    const i32 damage = gameplay::rangedDamage(
        p.damage, [&](std::string_view type) { return target->character->protection(type); }, m_combat);
    const i32 hp = target->character->attribute("hp") - damage;
    (void)target->character->setAttribute("hp", std::max(hp, 0));
    target->character->addItem(p.ammo, 1); // R3: it stays in the target (looting)
    const std::string shooter = p.shooter == kHeroId ? std::string("hero") : creature(p.shooter)->species;
    G7_LOG_INFO("engine", "{} shoots {}: {} damage ({} left)", shooter, target->name, damage,
                std::max(hp, 0));
    if (m_scripts)
    {
        const Value args[] = {shooter, target->name, Value(static_cast<i64>(damage)), Value(false)};
        m_scripts->emit("npc_hit", args);
    }
    if (hp > 0)
    {
        target->fighter->stagger();
        target->fighter->useTimeline();
        if (target->creature != nullptr && target->animal)
        {
            target->creature->action = 3;
        }
        return;
    }
    if (target->id == kHeroId)
    {
        (void)target->character->setAttribute("hp", 1); // K8: the hero gets up again
        target->fighter->knockOut(5.0f);
        return;
    }
    // K7: ranged combat kills.
    target->fighter->die();
    target->creature->dead = true;
    stopForFight(*target->creature);
    if (m_scripts)
    {
        const Value args[] = {target->name, shooter};
        m_scripts->emit("npc_killed", args);
    }
}

} // namespace g7

// NPCs using mobs (plan approved 2026-10-08, like Gothic's AI_UseMob): a command takes the nearest mob of a
// type with a free slot - its own first (the mob's owner is the NPC or its guild), then one in the same house
// as the place its state was started for (the indoor zones' LEO_<USE>_<CODE>, world.md), then the nearest -
// walks there over the waynet, turns to the slot and plays <type>_enter, then the loop (or a variant:
// table_drink, table_talk) until it leaves (<type>_leave). Hero and NPCs share the slots.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Mobs.hpp>
#include <g7/runtime/Engine.hpp>

#include <cmath>
#include <format>

namespace g7
{
namespace
{
constexpr f32 kTurnRate = 4.0f;          ///< rad/s turning to the slot
constexpr f32 kUnanimatedSeconds = 0.6f; ///< a phase without a clip in the graph
constexpr f32 kArriveDistance = 0.6f;    ///< m: near enough to the slot to sit down (then placed exactly)
constexpr f32 kApproachMargin = 0.3f; ///< m outside the slot the way ends (the capsule's radius)
constexpr std::string_view kDrinkItem = "it_mug"; ///< in the hand in table_drink (figuren #281)

f32 wrap(f32 a)
{
    return std::remainder(a, 2.0f * glm::pi<f32>());
}
} // namespace

std::string Engine::houseAt(const Vec3& point) const
{
    const world::Zone* zone = smallestZoneAt("indoor", point, nullptr);
    if (zone == nullptr)
    {
        return {};
    }
    // LEO_<USE>_<CODE>_<ROOM...>: the first three parts name the house.
    usize cut = 0;
    for (int part = 0; part < 3; ++part)
    {
        cut = zone->value.find('_', cut == 0 && part == 0 ? 0 : cut + 1);
        if (cut == std::string::npos)
        {
            return zone->value;
        }
    }
    return zone->value.substr(0, cut);
}

bool Engine::startNpcMob(Creature& c, std::string_view type, f32 radius, std::string_view variant)
{
    const gameplay::MobType* mobType = m_mobTypes.find(type);
    if (mobType == nullptr || mobType->slots.empty())
    {
        G7_LOG_WARN("engine", "{}: npc_use_mob: type \"{}\" has no slots (data/mobs.toml)", c.species, type);
        return false;
    }
    const script::Instance* npc = m_scripts ? m_scripts->findInstance("Npc", c.species) : nullptr;
    const std::string guild = npc != nullptr ? std::string(npc->fields["guild"].asString()) : std::string();
    const std::string home = houseAt(navigationTarget(c.stateAt).value_or(c.position));
    // Best: own (owner), then the same house, then the nearest; within the radius.
    struct Candidate
    {
        u64 vob = 0;
        gameplay::SlotPlace place;
        int rank = 3;
        f32 distance = 0.0f;
    };
    std::optional<Candidate> best;
    for (auto& [id, m] : m_mobs)
    {
        if (m.type != type)
        {
            continue;
        }
        const Mat4 world = mobRestMatrix(world::VobId{id});
        const Vec3 at = Vec3(world[3]);
        const f32 distance = glm::length(at - c.position);
        if (radius > 0.0f && distance > radius)
        {
            continue;
        }
        const auto place = gameplay::chooseSlot(*mobType, world, c.position, busySlots(m, c.species));
        if (!place)
        {
            continue; // full
        }
        const bool own = !m.owner.empty() && (m.owner == c.species || (!guild.empty() && m.owner == guild));
        const bool sameHouse = !home.empty() && houseAt(at + Vec3(0.0f, 0.5f, 0.0f)) == home;
        const int rank = own ? 0 : sameHouse ? 1 : 2;
        if (!best || rank < best->rank || (rank == best->rank && distance < best->distance))
        {
            best = Candidate{id, *place, rank, distance};
        }
    }
    if (!best)
    {
        G7_LOG_DEBUG("engine", "{}: no free {} within {} m", c.species, type, radius);
        return false;
    }
    m_mobs[best->vob].occupants[static_cast<u32>(best->place.index)] = c.species;
    Creature::MobUse use;
    use.vob = best->vob;
    use.slot = static_cast<u32>(best->place.index);
    use.feet = best->place.feet;
    use.yaw = best->place.yaw;
    use.type = std::string(type);
    use.variant = std::string(variant);
    c.mob = std::move(use);
    if (glm::length(c.mob->feet - c.position) > kArriveDistance)
    {
        // To a point a little outside the slot (it may touch the bench with the capsule's radius); on arrival
        // the NPC is set onto the slot, as the hero.
        const Vec3 centre = Vec3(mobRestMatrix(world::VobId{best->vob})[3]);
        Vec3 out = c.mob->feet - centre;
        out.y = 0.0f;
        const Vec3 goal =
            glm::length(out) > 1e-3f ? c.mob->feet + glm::normalize(out) * kApproachMargin : c.mob->feet;
        if (auto sent = npcGoToPosition(c.id, goal, "mob", false); !sent)
        {
            G7_LOG_WARN("engine", "{}: {}", c.species, sent.error().message);
            releaseNpcMob(c);
            return false;
        }
    }
    return true;
}

bool Engine::stepNpcMob(Creature& c, f32 seconds)
{
    if (!c.mob)
    {
        return true;
    }
    Creature::MobUse& use = *c.mob;
    animation::Animator* a = c.figure ? &c.figure->animator : nullptr;
    use.time += seconds;
    const auto play = [&](std::string state)
    {
        use.time = 0.0f;
        use.state = a != nullptr && a->hasState(state) ? std::move(state) : std::string();
        if (!use.state.empty())
        {
            a->enter(use.state, 0.2f);
        }
    };
    const auto clipDone = [&]
    {
        return use.state.empty() ? use.time >= kUnanimatedSeconds
                                 : (a == nullptr || a->state() != use.state || a->stateEnded());
    };
    switch (use.phase)
    {
    case Creature::MobUse::Phase::Approach:
        if (c.route)
        {
            return false;
        }
        // Arrived (or near): exactly onto the slot, then turn.
        if (c.body)
        {
            c.body->teleport(use.feet);
            c.position = c.positionBefore = c.body->feet();
        }
        else
        {
            c.position = c.positionBefore = use.feet;
        }
        use.phase = Creature::MobUse::Phase::Turn;
        return false;
    case Creature::MobUse::Phase::Turn:
    {
        const f32 turn = wrap(use.yaw - c.yaw);
        c.yaw = wrap(c.yaw + std::clamp(turn, -kTurnRate * seconds, kTurnRate * seconds));
        if (std::abs(turn) < 0.05f)
        {
            c.yaw = use.yaw;
            use.phase = Creature::MobUse::Phase::Enter;
            play(use.type + "_enter");
        }
        return false;
    }
    case Creature::MobUse::Phase::Enter:
        if (!clipDone())
        {
            return false;
        }
        use.phase = Creature::MobUse::Phase::Loop;
        if (!use.variant.empty() && a != nullptr && a->hasState(std::format("{}_{}", use.type, use.variant)))
        {
            if (use.variant == "drink")
            {
                c.handItemWanted = std::string(kDrinkItem); // the clip's item_to_hand takes it
            }
            play(std::format("{}_{}", use.type, use.variant));
        }
        else
        {
            play(use.type + "_loop");
        }
        return true; // sitting: the command is done, the NPC stays until it leaves
    case Creature::MobUse::Phase::Loop:
        return true;
    case Creature::MobUse::Phase::Leave:
        if (!clipDone())
        {
            return false;
        }
        if (a != nullptr && a->hasState("move"))
        {
            a->enter("move", 0.25f);
        }
        releaseNpcMob(c);
        return true;
    }
    return true;
}

bool Engine::startLeaveNpcMob(Creature& c)
{
    if (!c.mob)
    {
        return false;
    }
    Creature::MobUse& use = *c.mob;
    if (use.phase == Creature::MobUse::Phase::Approach || use.phase == Creature::MobUse::Phase::Turn)
    {
        c.route.reset();
        releaseNpcMob(c); // not seated yet: nothing to stand up from
        return false;
    }
    // The mug goes (figuren: s_sit_drink only puts it into the hand); then stand up.
    c.handItem = nullptr;
    c.handItemWanted.clear();
    use.phase = Creature::MobUse::Phase::Leave;
    animation::Animator* a = c.figure ? &c.figure->animator : nullptr;
    const std::string leave = use.type + "_leave";
    use.time = 0.0f;
    use.state = a != nullptr && a->hasState(leave) ? leave : std::string();
    if (!use.state.empty())
    {
        a->enter(use.state, 0.2f);
    }
    return true;
}

void Engine::releaseNpcMob(Creature& c)
{
    if (!c.mob)
    {
        return;
    }
    if (const auto it = m_mobs.find(c.mob->vob); it != m_mobs.end())
    {
        if (const auto slot = it->second.occupants.find(c.mob->slot);
            slot != it->second.occupants.end() && slot->second == c.species)
        {
            it->second.occupants.erase(slot);
        }
    }
    if (c.mob->phase == Creature::MobUse::Phase::Leave && c.body)
    {
        c.body->teleport(c.mob->feet); // standing at the slot, free of the mob (variant A)
        c.position = c.positionBefore = c.body->feet();
    }
    c.mob.reset();
}
} // namespace g7

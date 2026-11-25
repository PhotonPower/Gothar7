// Monsters of the dungeon (dungeon D6, docs/modules/dungeon.md "Monster im Dungeon"): spawn markers put their
// NPCs in when a world is new, nests fill up again when the hero comes back, and the scripts get what they
// need to let them rest and wake (zs_dormant in game/scripts/ai/monsters.lua): who belongs to which marker,
// whether one still rests, items held in the mouth and the clip events of the dungeon species.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
constexpr f32 kRingRadius = 1.5f; ///< m: the second and further NPCs of a marker stand around it
constexpr f32 kProbeAbove = 0.5f; ///< m: the ground below a marker (a room's ceiling is not ground)
constexpr f32 kProbeBelow = 3.0f; ///< m: how far below the marker the ground may lie
constexpr f32 kDropAhead = 0.4f;  ///< m: an item an NPC lets fall lies this far before its feet
constexpr std::array<std::string_view, 6> kScriptEvents = {
    "steal", "emerge", "grab", "release", "reflect", "spawn_brood"}; ///< characters-pipeline.md 7.5

f32 yawOfMatrix(const Mat4& m)
{
    const Vec3 ahead = Vec3(m * Vec4(0.0f, 0.0f, -1.0f, 0.0f));
    return std::atan2(-ahead.x, -ahead.z);
}
} // namespace

std::vector<std::string> Engine::spawnAt(entt::entity e, u32 first, u32 count)
{
    std::vector<std::string> names;
    const world::Spawn* spawn = m_scene.get<world::Spawn>(e);
    const world::Vob* vob = m_scene.get<world::Vob>(e);
    if (spawn == nullptr || vob == nullptr)
    {
        return names;
    }
    const Mat4 m = m_scene.worldMatrix(e);
    const Vec3 centre(m[3]);
    const f32 yaw = yawOfMatrix(m);
    for (u32 i = first; i < first + count; ++i)
    {
        Vec3 at = centre;
        if (i > 0)
        {
            const u32 ring = std::max(spawn->count, 2u) - 1;
            const f32 angle = static_cast<f32>(i - 1) * 2.0f * glm::pi<f32>() / static_cast<f32>(ring);
            at += Vec3(std::sin(angle), 0.0f, std::cos(angle)) * kRingRadius;
        }
        if (m_physics.valid())
        {
            if (const auto hit =
                    m_physics.raycast(at + Vec3(0.0f, kProbeAbove, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
                                      kProbeAbove + kProbeBelow, physics::layerBit(physics::Layer::World)))
            {
                at.y = hit->position.y;
            }
        }
        auto spawned = spawnNpc(spawn->npc, at, yaw);
        if (!spawned)
        {
            G7_LOG_WARN("engine", "spawn {}: {}", vob->nameText, spawned.error().message);
            break;
        }
        Creature* c = creature(spawned.value());
        c->spawn = vob->nameText;
        c->dormant = spawn->dormant;
        names.push_back(c->species);
    }
    return names;
}

void Engine::spawnFromMarkers()
{
    std::vector<entt::entity> markers;
    m_scene.each<world::Vob, world::Spawn>([&](entt::entity e, const world::Vob&, const world::Spawn&)
                                           { markers.push_back(e); });
    std::ranges::sort(markers, {},
                      [&](entt::entity e) { return m_scene.get<world::Vob>(e)->id.value; }); // stable names
    std::vector<std::string> names;
    for (const entt::entity e : markers)
    {
        const auto put = spawnAt(e, 0, m_scene.get<world::Spawn>(e)->count);
        names.insert(names.end(), put.begin(), put.end());
    }
    if (!names.empty())
    {
        G7_LOG_INFO("engine", "{} monsters from {} spawn markers", names.size(), markers.size());
    }
    for (const std::string& name : names) // after all: a handler may look at the others
    {
        if (const auto id = npcByInstance(name))
        {
            emitSpawned(*creature(*id), false);
        }
    }
}

void Engine::refillNests()
{
    std::vector<std::string> names;
    std::vector<entt::entity> nests;
    m_scene.each<world::Vob, world::Spawn>(
        [&](entt::entity e, const world::Vob&, const world::Spawn& spawn)
        {
            if (spawn.respawn)
            {
                nests.push_back(e);
            }
        });
    for (const entt::entity e : nests)
    {
        const std::string& name = m_scene.get<world::Vob>(e)->nameText;
        // The dead of the nest go (their bodies with them), the living stay.
        std::erase_if(m_creatures,
                      [&](const auto& c) { return c->spawn == name && (c->dead || c->vanished); });
        const auto living = static_cast<u32>(std::count_if(m_creatures.begin(), m_creatures.end(),
                                                           [&](const auto& c) { return c->spawn == name; }));
        const u32 count = m_scene.get<world::Spawn>(e)->count;
        if (living < count)
        {
            const auto put = spawnAt(e, living, count - living);
            G7_LOG_INFO("engine", "nest {}: {} new", name, put.size());
            names.insert(names.end(), put.begin(), put.end());
        }
    }
    for (const std::string& name : names)
    {
        if (const auto id = npcByInstance(name))
        {
            emitSpawned(*creature(*id), false);
        }
    }
}

void Engine::emitSpawned(const Creature& c, bool restored)
{
    if (m_scripts)
    {
        const script::Value args[] = {c.species, c.spawn, c.dormant, restored};
        m_scripts->emit("npc_spawned", args);
    }
}

void Engine::creatureScriptEvent(const Creature& c, std::string_view event)
{
    if (m_scripts && c.character &&
        std::find(kScriptEvents.begin(), kScriptEvents.end(), event) != kScriptEvents.end())
    {
        const script::Value args[] = {c.species, std::string(event)};
        m_scripts->emit("npc_clip_event", args);
    }
}

void Engine::bindSpawnFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    const auto npc = [this](std::span<const Value> a) -> Result<Creature*>
    {
        const auto id = a.empty() || !a[0].isString() ? std::nullopt : npcByInstance(a[0].asString());
        if (!id)
        {
            return Error{std::format("no NPC \"{}\" in this world", a.empty() ? "" : a[0].asString())};
        }
        return creature(*id);
    };
    vm.bind({"spawn_npcs", "spawn_npcs(spawn: string) -> {string}",
             "Die lebenden NPCs, die ein Spawn-Marker (Vob-Typ `spawn`, Dungeon D6) eingesetzt hat, in der "
             "Reihenfolge des Einsetzens.",
             "NPCs", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"argument 1 must be a spawn marker's name"};
                 }
                 std::vector<Value> list;
                 for (const auto& c : m_creatures)
                 {
                     if (!c->spawn.empty() && c->spawn == a[0].asString() && !c->dead && !c->vanished)
                     {
                         list.emplace_back(c->species);
                     }
                 }
                 return script::makeTable(std::move(list));
             }});
    vm.bind(
        {"npc_dormant", "npc_dormant(npc: string) -> boolean",
         "Ob der NPC noch ruht (Dungeon D6: eingesetzt mit `dormant`, bis `npc_set_dormant(npc, false)`). "
         "Wird gespeichert.",
         "NPCs", [npc](std::span<const Value> a) -> Result<Value>
         {
             auto c = npc(a);
             if (!c)
             {
                 return c.error();
             }
             return Value(c.value()->dormant);
         }});
    vm.bind({"npc_set_dormant", "npc_set_dormant(npc: string, dormant: boolean)",
             "Setzt, ob der NPC ruht (Dungeon D6); `false` beendet auch seine Ruhe-Pose. Das Verhalten dazu "
             "steht "
             "in `zs_dormant` (ai/monsters.lua).",
             "NPCs", [npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c || a.size() < 2)
                 {
                     return !c ? c.error() : Error{"argument 2 must be true or false"};
                 }
                 c.value()->dormant = a[1].asBool();
                 if (!c.value()->dormant)
                 {
                     c.value()->rest = false; // awake: out of its resting pose at once (it may fight next)
                     c.value()->sleep = false;
                 }
                 return Value();
             }});
    vm.bind(
        {"npc_set_weak", "npc_set_weak(npc: string, open: boolean)",
         "Öffnet bzw. schließt die Schwachstelle eines Monsters (`Creatures.<art>.weak`; die Kellermutter in "
         "Phase 3): offen zählen Treffer von hinten mehrfach, schwere lassen es taumeln.",
         "NPCs", [npc](std::span<const Value> a) -> Result<Value>
         {
             auto c = npc(a);
             if (!c || a.size() < 2)
             {
                 return !c ? c.error() : Error{"argument 2 must be true or false"};
             }
             c.value()->weakOpen = a[1].asBool();
             return Value();
         }});
    vm.bind({"npc_weak", "npc_weak(npc: string) -> boolean",
             "Ob die Schwachstelle offen ist (`npc_set_weak`).", "NPCs",
             [npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c)
                 {
                     return c.error();
                 }
                 return Value(c.value()->weakOpen);
             }});
    vm.bind({"npc_hold", "npc_hold(npc: string, item?: string, socket?: string)",
             "Zeigt einen Gegenstand am Socket der Figur (Vorgabe `socket_hand_r`; ein Mauerschlupfer trägt "
             "Gestohlenes am `socket_mouth`). Ohne `item` legt er ihn weg. Nur die Anzeige: den Gegenstand "
             "selbst "
             "gibt `npc_give_item`.",
             "NPCs", [this, npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c)
                 {
                     return c.error();
                 }
                 Creature& creature = *c.value();
                 const std::string item = a.size() > 1 ? std::string(a[1].asString()) : std::string();
                 if (item.empty())
                 {
                     creature.heldItem.clear();
                     creature.handItem = nullptr;
                     return Value();
                 }
                 if (!m_scripts->findInstance("Item", item))
                 {
                     return Error{std::format("no Item \"{}\"", item)};
                 }
                 const std::string socket = a.size() > 2 && !a[2].asString().empty()
                                                ? std::string(a[2].asString())
                                                : "socket_hand_r";
                 const i32 bone = creature.figure ? creature.figure->skeleton.find(socket) : -1;
                 if (bone < 0)
                 {
                     return Error{std::format("{} has no bone \"{}\"", creature.species, socket)};
                 }
                 creature.handBone = static_cast<usize>(bone);
                 creature.handItemWanted = item;
                 creature.heldItem = item;
                 creature.handItem = itemModel(item);
                 return Value();
             }});
    vm.bind({"npc_drop_item", "npc_drop_item(npc: string, item: string, count?: integer) -> boolean",
             "Der NPC lässt `count` (Vorgabe 1) Stück aus seinem Inventar vor sich fallen (auch tot: ein "
             "Mauerschlupfer verliert, was er gestohlen hat); `false`, wenn er weniger hat.",
             "NPCs", [this, npc](std::span<const Value> a) -> Result<Value>
             {
                 auto c = npc(a);
                 if (!c || a.size() < 2 || !a[1].isString())
                 {
                     return !c ? c.error() : Error{"argument 2 must be an Item"};
                 }
                 Creature& creature = *c.value();
                 const auto count = static_cast<u32>(std::max<i64>(1, a.size() > 2 ? a[2].asInteger(1) : 1));
                 if (!creature.character || !creature.character->removeItem(a[1].asString(), count))
                 {
                     return Value(false);
                 }
                 if (creature.heldItem == a[1].asString() &&
                     creature.character->itemCount(a[1].asString()) == 0)
                 {
                     creature.heldItem.clear();
                     creature.handItem = nullptr;
                 }
                 const Vec3 at = creature.position + gameplay::forwardOf(creature.yaw) * kDropAhead;
                 if (auto dropped = spawnItem(a[1].asString(), count, at, creature.yaw); !dropped)
                 {
                     return dropped.error();
                 }
                 return Value(true);
             }});
    vm.bind(
        {"npc_spawned",
         "on(\"npc_spawned\", fn(npc: string, spawn: string, dormant: boolean, restored: boolean))",
         "Ein Spawn-Marker hat einen NPC eingesetzt (Dungeon D6: beim ersten Laden der Welt, in einem Nest "
         "auch beim Wiederkommen). Nach dem Laden eines Spielstands bzw. beim Wiederkommen in eine Ebene "
         "kommt es noch einmal für jeden lebenden mit `restored = true`: sein Zustand beginnt neu.",
         "Ereignisse",
         {}});
    vm.bind({"npc_clip_event",
             "on(\"npc_clip_event\", fn(npc: string, event: string))",
             "Ein Clip-Event eines Monsters, auf das die Skripte reagieren (`steal`, `emerge`, `grab`, "
             "`release`, `reflect`, `spawn_brood`; characters-pipeline.md §7.5).",
             "Ereignisse",
             {}});
}
} // namespace g7

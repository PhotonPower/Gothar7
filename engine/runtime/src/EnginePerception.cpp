// NPC perception (M9 part C, docs/modules/ai.md "Wahrnehmung"): sight (cone, range, line of sight; sneaking
// and night shorten it), hearing (noises with a radius) and the player's deeds witnessed. What an NPC makes
// of it is content: the engine emits assess_* events with the NPC first (game/scripts/ai/perceptions.lua).
// Also the hero drawing his weapon (draw_weapon).

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Character.hpp>
#include <g7/physics/Physics.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
constexpr f32 kEyeHeight = 1.6f;   ///< metres above the feet
constexpr f32 kChestHeight = 1.2f; ///< what an NPC looks at on the player
constexpr f64 kNoiseLifetime =
    2.0; ///< seconds a noise can still be heard (the slowest NPCs look every second)

f64 simSeconds(u64 ticks, f64 step)
{
    return static_cast<f64>(ticks) * step;
}
} // namespace

void Engine::loadPerceptionSettings()
{
    m_perception = PerceptionSettings{};
    if (!m_scripts)
    {
        return;
    }
    const script::Value table = m_scripts->global("Perception");
    if (table.asTable() == nullptr)
    {
        return;
    }
    const auto number = [&](std::string_view key, f32& out)
    { out = static_cast<f32>(table[key].asNumber(static_cast<f64>(out))); };
    number("sight", m_perception.sight);
    number("angle", m_perception.angle);
    number("sneak_factor", m_perception.sneakFactor);
    number("night_factor", m_perception.nightFactor);
    number("near_distance", m_perception.nearDistance);
    number("forget_seconds", m_perception.forgetSeconds);
    number("room_distance", m_perception.roomDistance);
    if (const script::Table* noises = table["noise"].asTable())
    {
        for (const auto& [kind, radius] : noises->fields)
        {
            m_perception.noises.emplace_back(kind, static_cast<f32>(radius.asNumber(0.0)));
        }
    }
}

f32 Engine::noiseRadius(std::string_view kind) const
{
    for (const auto& [name, radius] : m_perception.noises)
    {
        if (name == kind)
        {
            return radius;
        }
    }
    return 0.0f;
}

void Engine::emitNoise(const Vec3& at, f32 radius, std::string_view kind)
{
    if (radius <= 0.0f)
    {
        return;
    }
    const f64 now = simSeconds(m_simTicks, m_fixedStep.step());
    std::erase_if(m_noises, [&](const Noise& n) { return n.time < now - kNoiseLifetime; });
    m_noises.push_back({at, radius, std::string(kind), now});
}

bool Engine::seesPlayer(const Creature& c) const
{
    if (!m_player.valid())
    {
        return false;
    }
    const Vec3 eye = c.position + Vec3(0.0f, kEyeHeight, 0.0f);
    const Vec3 target = m_player.feet() + Vec3(0.0f, kChestHeight, 0.0f);
    Vec3 to = target - eye;
    const f32 distance = glm::length(to);
    // Range: shorter for a sneaking player and at night (Gothic: sneaking past sleepers and in the dark).
    f32 range = c.sight;
    if (m_playerInputOverride.value_or(m_playerInput).sneak)
    {
        range *= m_perception.sneakFactor;
    }
    const f64 hour = m_gameTime.minuteOfDay() / 60.0;
    if (hour < 6.0 || hour >= 21.0)
    {
        range *= m_perception.nightFactor;
    }
    if (distance > range || distance < 1e-3f)
    {
        return distance < 1e-3f;
    }
    // Cone: horizontal, around where the NPC faces.
    const Vec3 flat = glm::normalize(Vec3(to.x, 0.0f, to.z) + Vec3(1e-6f, 0.0f, 0.0f));
    if (glm::dot(flat, gameplay::forwardOf(c.yaw)) < c.sightCos)
    {
        return false;
    }
    // Line of sight: the world in between (not other creatures).
    to /= distance;
    return !m_physics.valid() ||
           !m_physics.raycast(eye, to, distance - 0.3f, physics::layerBit(physics::Layer::World)).has_value();
}

bool Engine::npcSeesPlayer(u32 id) const
{
    const Creature* c = creature(id);
    return c != nullptr && c->character && seesPlayer(*c);
}

bool Engine::ownedBy(const Creature& c, std::string_view owner) const
{
    return !owner.empty() && (c.species == owner || (c.character && c.character->guild() == owner));
}

void Engine::perceive(Creature& c, f32 seconds)
{
    if (!m_scripts || !c.character)
    {
        return;
    }
    const Vec3 player = m_player.valid() ? m_player.feet() : m_camera.transform.position;
    const f32 distance = glm::length(player - c.position);
    // Staggered and by distance: 5 Hz near the player, once a second farther away.
    c.perceptionTimer -= seconds;
    if (c.perceptionTimer > 0.0f)
    {
        return;
    }
    c.perceptionTimer += distance < m_perception.nearDistance ? 0.2f : 1.0f;
    const f64 now = simSeconds(m_simTicks, m_fixedStep.step());

    // Sight.
    const bool sees = seesPlayer(c);
    if (sees && !c.seesPlayer && now - c.lastSawPlayer > m_perception.forgetSeconds)
    {
        const script::Value args[] = {c.species, static_cast<f64>(distance)};
        m_scripts->emit("assess_player", args);
    }
    c.seesPlayer = sees;
    if (sees)
    {
        c.lastSawPlayer = now;
        // Every look while he is in sight (animals threaten when he comes nearer; M9 part D).
        const script::Value args[] = {c.species, static_cast<f64>(distance)};
        m_scripts->emit("observe_player", args);
    }
    if (m_weaponMode == 0)
    {
        c.reportedFighter = false;
    }
    else if (sees && !c.reportedFighter)
    {
        c.reportedFighter = true;
        const script::Value args[] = {c.species, static_cast<f64>(distance),
                                      std::string(m_weaponMode == 2   ? "fists"
                                                  : m_weaponMode == 4 ? "magic"
                                                  : m_weaponMode == 5 ? "animal"
                                                                      : "weapon")};
        m_scripts->emit("assess_fighter", args);
    }

    dialogPerception(c, distance, sees); // important Infos (M10)

    // Hearing: noises since the last look.
    for (const Noise& n : m_noises)
    {
        if (n.time <= c.heardUntil || glm::length(n.at - c.position) > n.radius * c.hearing)
        {
            continue;
        }
        const script::Value args[] = {c.species, n.kind, static_cast<f64>(n.at.x), static_cast<f64>(n.at.y),
                                      static_cast<f64>(n.at.z)};
        m_scripts->emit("assess_noise", args);
    }
    c.heardUntil = now;
}

void Engine::witnessed(std::string_view event, std::span<const script::Value> arguments,
                       std::string_view alsoNpc)
{
    if (!m_scripts)
    {
        return;
    }
    // Who sees it now (not at the next look: the deed is over by then).
    std::vector<std::string> witnesses;
    for (const auto& owned : m_creatures)
    {
        // `alsoNpc` notices it anyway (the victim of a failed pickpocketing).
        if (owned->character && !owned->vanished &&
            (owned->species == alsoNpc || (owned->simulated && seesPlayer(*owned))) &&
            std::ranges::find(witnesses, owned->species) == witnesses.end())
        {
            witnesses.push_back(owned->species);
        }
    }
    for (const std::string& npc : witnesses)
    {
        std::vector<script::Value> args{npc};
        args.insert(args.end(), arguments.begin(), arguments.end());
        m_scripts->emit(event, args);
    }
}

void Engine::enteredPrivateArea(std::string_view owner, std::string_view area)
{
    if (!m_scripts || !m_player.valid())
    {
        return;
    }
    std::vector<std::string> owners;
    for (const auto& owned : m_creatures)
    {
        const Creature& c = *owned;
        // The owner notices if he sees the player or is near (in the room, behind the door).
        if (c.character && c.simulated && !c.vanished && ownedBy(c, owner) &&
            (seesPlayer(c) || glm::length(m_player.feet() - c.position) < m_perception.roomDistance))
        {
            owners.push_back(c.species);
        }
    }
    for (const std::string& npc : owners)
    {
        const script::Value args[] = {npc, std::string(owner), std::string(area)};
        m_scripts->emit("assess_enter_room", args);
    }
}

void Engine::toggleWeapon()
{
    if (!m_figure || m_transform)
    {
        return; // Z7: no weapons in an animal's shape
    }
    if (m_weaponMode != 0)
    {
        m_weaponMode = 0; // the model goes at the clip's "sheath" event (weaponEvent)
        if (!m_figure->animator.hasState("sheath_1h"))
        {
            weaponEvent("sheath");
        }
        return;
    }
    const gameplay::Character* h = hero();
    const std::string weapon = h != nullptr ? h->equipped(gameplay::EquipSlot::Melee) : std::string();
    if (weapon.empty() && !rangedWeapon().empty())
    {
        toggleRanged(); // R1: without a melee weapon the draw key takes the bow
        return;
    }
    m_weaponMode = weapon.empty() ? 2 : 1;
    if (m_torch && !weapon.empty() && m_scripts && m_scripts->findInstance("Item", weapon) != nullptr &&
        m_scripts->findInstance("Item", weapon)->fields["category"].asString() == "melee_2h")
    {
        putTorchAway(); // both hands for the two-handed weapon
    }
    m_weaponDrawn = weapon; // in the hand at the clip's "draw" event
    if (!m_figure->animator.hasState("draw_1h"))
    {
        weaponEvent("draw");
    }
}

bool Engine::playerHolds(std::string_view socket) const
{
    return m_figure && std::ranges::any_of(m_figure->attachments,
                                           [&](const FigureAttachment& a) { return a.socket == socket; });
}

void Engine::weaponEvent(std::string_view event)
{
    if (event == "sheath" && m_weaponMode == 0)
    {
        detachFromPlayer("socket_hand_r");
        if (!m_torch)
        {
            detachFromPlayer("socket_hand_l"); // a bow (M11) - not a torch held in it
        }
    }
    else if (event == "draw" && m_weaponMode == 3 && !m_weaponDrawn.empty())
    {
        // The bow in the left hand, the crossbow in the right (figuren, F6).
        if (const LoadedModel* model = itemModel(m_weaponDrawn))
        {
            const char* socket = rangedIsCrossbow(m_weaponDrawn) ? "socket_hand_r" : "socket_hand_l";
            if (auto attached = attachModel(socket, model, nullptr); !attached)
            {
                G7_LOG_WARN("engine", "draw ranged: {}", attached.error().message);
            }
        }
    }
    else if (event == "draw" && m_weaponMode == 4 && !m_weaponDrawn.empty())
    {
        // The rune or scroll in the right hand (M12).
        if (const LoadedModel* model = itemModel(m_weaponDrawn))
        {
            if (auto attached = attachModel("socket_hand_r", model, nullptr); !attached)
            {
                G7_LOG_WARN("engine", "draw magic: {}", attached.error().message);
            }
        }
    }
    else if (event == "draw" && m_weaponMode == 1 && !m_weaponDrawn.empty())
    {
        if (const LoadedModel* model = itemModel(m_weaponDrawn))
        {
            if (auto attached = attachModel("socket_hand_r", model, nullptr); !attached)
            {
                G7_LOG_WARN("engine", "draw weapon: {}", attached.error().message);
            }
        }
    }
}

void Engine::bindPerceptionFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"noise", "noise(x: number, y: number, z: number, radius: number, kind?: string)",
             "Ein Geräusch: NPCs im Umkreis (mal ihrem Gehör) bekommen `assess_noise(npc, kind, x, y, z)`.",
             "Wahrnehmung", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.size() < 4 || !a[0].isNumber() || !a[1].isNumber() || !a[2].isNumber() ||
                     !a[3].isNumber())
                 {
                     return Error{"expects (x, y, z, radius, kind?)"};
                 }
                 emitNoise(Vec3(static_cast<f32>(a[0].asNumber()), static_cast<f32>(a[1].asNumber()),
                                static_cast<f32>(a[2].asNumber())),
                           static_cast<f32>(a[3].asNumber()), a.size() > 4 ? a[4].asString() : "script");
                 return Value();
             }});
    vm.bind({"npc_sees_player", "npc_sees_player(npc: string) -> boolean",
             "Ob der NPC den Spieler gerade sieht (Sichtkegel, Reichweite, freie Sicht).", "Wahrnehmung",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 const auto id = a.empty() ? std::nullopt : npcByInstance(a[0].asString());
                 return Value(id && npcSeesPlayer(*id));
             }});
    vm.bind({"npc_distance_to_player", "npc_distance_to_player(npc: string) -> number",
             "Abstand des NPCs zum Spieler in Metern.", "Wahrnehmung",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 const auto id = a.empty() ? std::nullopt : npcByInstance(a[0].asString());
                 if (!id || !m_player.valid())
                 {
                     return Error{"no such NPC (or no player)"};
                 }
                 return Value(static_cast<f64>(glm::length(creature(*id)->position - m_player.feet())));
             }});
    vm.bind({"npcs_near", "npcs_near(npc: string, radius: number) -> {{npc, guild, distance}, ...}",
             "Die anderen simulierten NPCs im Umkreis des NPCs, nach Abstand sortiert (Hilferufe, Gruppen).",
             "Wahrnehmung", [this](std::span<const Value> a) -> Result<Value>
             {
                 const auto id = a.empty() ? std::nullopt : npcByInstance(a[0].asString());
                 if (!id || a.size() < 2 || !a[1].isNumber())
                 {
                     return Error{"expects (npc: string, radius: number) for an NPC in this world"};
                 }
                 const Creature& self = *creature(*id);
                 const f32 radius = static_cast<f32>(a[1].asNumber());
                 std::vector<std::pair<f32, const Creature*>> near;
                 for (const auto& owned : m_creatures)
                 {
                     const f32 d = glm::length(owned->position - self.position);
                     if (owned.get() != &self && owned->character && owned->simulated && !owned->vanished &&
                         d <= radius)
                     {
                         near.emplace_back(d, owned.get());
                     }
                 }
                 std::ranges::sort(near, {}, &std::pair<f32, const Creature*>::first);
                 std::vector<Value> list;
                 for (const auto& [d, c] : near)
                 {
                     list.push_back(script::makeTable({}, {{"npc", c->species},
                                                           {"guild", c->character->guild()},
                                                           {"distance", static_cast<f64>(d)}}));
                 }
                 return script::makeTable(std::move(list), {});
             }});
    vm.bind({"player_weapon", "player_weapon() -> string",
             "Was der Held gezogen hat: `\"none\"`, `\"weapon\"` (Nahkampfwaffe), `\"fists\"`, `\"ranged\"` "
             "(Bogen, Armbrust), `\"magic\"` (Rune, Spruchrolle) oder `\"animal\"` (in Tiergestalt, Z7).",
             "Wahrnehmung", [this](std::span<const Value>) -> Result<Value>
             {
                 return Value(std::string(m_weaponMode == 0   ? "none"
                                          : m_weaponMode == 1 ? "weapon"
                                          : m_weaponMode == 3 ? "ranged"
                                          : m_weaponMode == 4 ? "magic"
                                          : m_weaponMode == 5 ? "animal"
                                                              : "fists"));
             }});
    vm.bind({"player_inside", "player_inside(area: string) -> boolean",
             "Ob der Spieler im Trigger `area` (Vob-Name, z. B. ein privater Bereich) steht.", "Wahrnehmung",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"expects (area: string)"};
                 }
                 bool inside = false;
                 bool found = false;
                 m_scene.each<world::Vob, world::TriggerVolume>(
                     [&](entt::entity, const world::Vob& vob, const world::TriggerVolume&)
                     {
                         if (vob.nameText == a[0].asString())
                         {
                             found = true;
                             inside = inside || m_triggers.isInside(vob.id, kCameraProbe);
                         }
                     });
                 if (!found)
                 {
                     return Error{std::format("no trigger \"{}\" in this world", a[0].asString())};
                 }
                 return Value(inside);
             }});
    vm.bind({"draw_weapon", "draw_weapon() -> string",
             "Zieht die ausgerüstete Nahkampfwaffe (ohne sie die Fäuste) bzw. steckt sie weg, wie die Taste "
             "draw_weapon; gibt zurück, was danach gezogen ist (wie player_weapon).",
             "Wahrnehmung", [this](std::span<const Value>) -> Result<Value>
             {
                 toggleWeapon();
                 return Value(std::string(m_weaponMode == 0   ? "none"
                                          : m_weaponMode == 1 ? "weapon"
                                          : m_weaponMode == 3 ? "ranged"
                                          : m_weaponMode == 4 ? "magic"
                                          : m_weaponMode == 5 ? "animal"
                                                              : "fists"));
             }});
    // Events (documentation only).
    vm.bind({"assess_player",
             "on(\"assess_player\", fn(npc: string, distance: number))",
             "Der NPC sieht den Spieler (neu, oder wieder nach `Perception.forget_seconds`).",
             "Ereignisse",
             {}});
    vm.bind({"observe_player",
             "on(\"observe_player\", fn(npc: string, distance: number))",
             "Bei jedem Blick (5- bzw. 1-mal je Sekunde), solange der NPC den Spieler sieht.",
             "Ereignisse",
             {}});
    vm.bind({"assess_fighter",
             "on(\"assess_fighter\", fn(npc: string, distance: number, what: string))",
             "Der NPC sieht den Spieler mit gezogener Waffe (`what`: `\"weapon\"`, `\"fists\"`, `\"magic\"` "
             "oder `\"animal\"` in Tiergestalt); einmal je Ziehen.",
             "Ereignisse",
             {}});
    vm.bind({"assess_noise",
             "on(\"assess_noise\", fn(npc: string, kind: string, x, y, z))",
             "Der NPC hört ein Geräusch (`kind`: `\"run\"`, `\"lockpick\"`, `\"lock_broken\"` … oder aus "
             "noise()).",
             "Ereignisse",
             {}});
    vm.bind({"assess_theft",
             "on(\"assess_theft\", fn(npc: string, owner: string, item: string, count: integer))",
             "Der NPC sieht, wie der Spieler etwas stiehlt bzw. beim Taschendiebstahl erwischt wird (`item` "
             "leer).",
             "Ereignisse",
             {}});
    vm.bind({"assess_use_mob",
             "on(\"assess_use_mob\", fn(npc: string, owner: string, mob: string))",
             "Der NPC sieht, wie der Spieler einen fremden Mob (Truhe, Tür …) benutzt oder knackt.",
             "Ereignisse",
             {}});
    vm.bind({"assess_enter_room",
             "on(\"assess_enter_room\", fn(npc: string, owner: string, area: string))",
             "Der Spieler betritt einen privaten Bereich (Trigger mit `owner`) des NPCs bzw. seiner Gilde; "
             "der NPC "
             "sieht ihn oder ist in der Nähe.",
             "Ereignisse",
             {}});
}
} // namespace g7

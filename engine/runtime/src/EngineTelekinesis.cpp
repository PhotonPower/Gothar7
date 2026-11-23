// Telekinesis (M17, plan "Magie-Nachträge" part T, owner decisions 1-8 of 2026-11-08): with the spell held
// an item lifts and floats to the hero's hand, costing mana every second; at the hand it goes into the
// inventory like picking it up (a stranger's item is theft); let go, out of mana, out of reach or hit, it
// falls straight down and lies there as an item vob again. The flight is gameplay::TelekinesisFlight
// (kinematic, a sphere sweep keeps it out of walls); the item is drawn where it floats.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace g7
{
namespace
{
constexpr f32 kEyeHeight = 1.6f;
constexpr f32 kGroundProbe = 60.0f; ///< metres a falling item looks down for the ground

AABB transformedBounds(const AABB& box, const Mat4& m)
{
    AABB out{Vec3(1e30f), Vec3(-1e30f)};
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 corner((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                          (i & 4) ? box.max.z : box.min.z);
        const Vec3 p = Vec3(m * Vec4(corner, 1.0f));
        out.min = glm::min(out.min, p);
        out.max = glm::max(out.max, p);
    }
    return out;
}

gameplay::TelekinesisSettings settingsOf(const gameplay::SpellInfo& spell)
{
    gameplay::TelekinesisSettings s;
    s.manaPerSecond = spell.manaPerSecond > 0.0f ? spell.manaPerSecond : s.manaPerSecond;
    s.range = spell.range > 0.0f ? spell.range : s.range;
    s.speed = spell.speed > 0.0f ? spell.speed : s.speed;
    s.lift = spell.lift > 0.0f ? spell.lift : s.lift;
    s.maxSize = spell.carry > 0.0f ? spell.carry : s.maxSize;
    s.blockedDrop = spell.blockedDrop.value_or(s.blockedDrop);
    return s;
}

const char* phaseName(gameplay::TelekinesisFlight::Phase phase)
{
    switch (phase)
    {
    case gameplay::TelekinesisFlight::Phase::Lift:
        return "lift";
    case gameplay::TelekinesisFlight::Phase::Float:
        return "float";
    case gameplay::TelekinesisFlight::Phase::Arrived:
        return "arrive";
    case gameplay::TelekinesisFlight::Phase::Falling:
    case gameplay::TelekinesisFlight::Phase::Landed:
        break;
    }
    return "drop";
}
} // namespace

bool Engine::beginTelekinesis(const gameplay::SpellInfo& spell)
{
    if (!m_hero || !m_player.valid())
    {
        return false;
    }
    const gameplay::TelekinesisSettings settings = settingsOf(spell);
    const Vec3 eye = m_playerFeet + Vec3(0.0f, kEyeHeight, 0.0f);
    const Vec3 aim = m_camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    std::vector<gameplay::TelekinesisCandidate> candidates;
    for (const WorldItem& item : m_worldItems)
    {
        if (m_telekinesis && m_telekinesis->vob == item.vob)
        {
            continue; // still falling from the last time
        }
        const entt::entity e = m_scene.findById(item.vob);
        const world::ItemRef* ref = e != entt::null ? m_scene.get<world::ItemRef>(e) : nullptr;
        if (ref == nullptr)
        {
            continue;
        }
        const script::Instance* def = m_scripts->findInstance("Item", ref->instance);
        const Vec3 size = item.bounds.max - item.bounds.min;
        candidates.push_back({item.vob.value, 0.5f * (item.bounds.min + item.bounds.max),
                              std::max({size.x, size.y, size.z}),
                              def == nullptr || !def->fields["no_telekinesis"].asBool()});
    }
    const auto visible = [&](const gameplay::TelekinesisCandidate& c)
    {
        if (!m_physics.valid())
        {
            return true;
        }
        // Looked at from a little above: at a grazing angle the ground it lies on would hide it.
        const Vec3 to = c.point + Vec3(0.0f, 0.3f, 0.0f) - eye;
        const f32 distance = glm::length(to);
        const auto hit =
            m_physics.raycast(eye, to / distance, distance, physics::layerBit(physics::Layer::World));
        return !hit || hit->distance >= distance - 0.3f;
    };
    // Along the camera's view, and along the hero's when the camera looks down at him from above.
    std::optional<u64> chosen = gameplay::pickTelekinesisTarget(candidates, eye, aim, settings, visible);
    if (!chosen)
    {
        chosen = gameplay::pickTelekinesisTarget(candidates, eye, gameplay::forwardOf(m_movement.yaw()),
                                                 settings, visible);
    }
    if (!chosen)
    {
        notice(uiText("UI_MAGIC_NOTHING_TO_MOVE"));
        if (m_figure && m_figure->animator.hasState("mag_t_cast_fail"))
        {
            m_figure->animator.enter("mag_t_cast_fail", 0.1f);
        }
        return false;
    }
    if (m_telekinesis)
    {
        finishTelekinesis(); // the last one lands at once
    }
    const auto item = std::find_if(m_worldItems.begin(), m_worldItems.end(),
                                   [&](const WorldItem& w) { return w.vob.value == *chosen; });
    const entt::entity e = m_scene.findById(item->vob);
    const world::ItemRef* ref = m_scene.get<world::ItemRef>(e);
    const Vec3 centre = 0.5f * (item->bounds.min + item->bounds.max);
    m_telekinesis.emplace(TelekinesisRun{
        item->vob, ref->instance, settings, gameplay::TelekinesisFlight(centre, settings), item->transform,
        centre, centre.y - item->bounds.min.y, std::nullopt, 0.0f, m_hero->attribute("hp")});
    // Its cost when it lifts (decision 1); a scroll is used up (Z3).
    (void)m_hero->setAttribute("mana", m_hero->attribute("mana") - spell.mana);
    if (m_heroCast && m_heroCast->scroll)
    {
        (void)m_hero->removeItem(m_heroCast->item, 1);
    }
    if (m_focus && m_focus->id == item->vob.value)
    {
        m_focus.reset();
    }
    if (const auto on = spell.fx.find("on_target"); on != spell.fx.end())
    {
        m_telekinesis->effect = startEffect(on->second, centre);
    }
    if (const auto sound = spell.sounds.find("cast"); sound != spell.sounds.end())
    {
        (void)playSound(sound->second, centre);
    }
    if (m_figure)
    {
        for (const char* state : {"mag_s_cast_loop", "mag_t_invest"})
        {
            if (m_figure->animator.hasState(state))
            {
                m_figure->animator.enter(state, 0.15f);
                break;
            }
        }
    }
    G7_LOG_INFO("engine", "telekinesis: {} lifts at ({:.1f}, {:.1f}, {:.1f})", m_telekinesis->item, centre.x,
                centre.y, centre.z);
    if (m_scripts)
    {
        const script::Value cast[] = {std::string("hero"), spell.instance};
        m_scripts->emit("npc_cast", cast);
        const script::Value args[] = {m_telekinesis->item, std::string("lift")};
        m_scripts->emit("telekinesis", args);
    }
    return true;
}

void Engine::placeTelekinesisItem()
{
    TelekinesisRun& t = *m_telekinesis;
    // Where it floats, turning slowly about the vertical through its middle.
    const Mat4 matrix = glm::translate(Mat4(1.0f), t.flight.position()) *
                        glm::rotate(Mat4(1.0f), t.flight.spin(), Vec3(0.0f, 1.0f, 0.0f)) *
                        glm::translate(Mat4(1.0f), -t.centre) * t.start;
    for (WorldItem& item : m_worldItems)
    {
        if (item.vob == t.vob)
        {
            item.transform = matrix;
            item.bounds = transformedBounds(item.model->bounds, matrix);
        }
    }
    if (t.effect)
    {
        m_particles.move(*t.effect, t.flight.position(), Vec3(0.0f, 1.0f, 0.0f));
    }
}

void Engine::dropTelekinesis()
{
    TelekinesisRun& t = *m_telekinesis;
    const Vec3 at = t.flight.position();
    f32 ground = at.y - t.halfHeight - kGroundProbe;
    if (m_physics.valid())
    {
        if (const auto hit = m_physics.raycast(at, Vec3(0.0f, -1.0f, 0.0f), kGroundProbe,
                                               physics::layerBit(physics::Layer::World)))
        {
            ground = hit->position.y;
        }
        else
        {
            ground = t.start[3].y; // nothing below (over a hole): back to where it lay
        }
    }
    else
    {
        ground = t.start[3].y;
    }
    t.flight.release(ground + t.halfHeight);
    G7_LOG_INFO("engine", "telekinesis: {} falls", t.item);
    if (m_scripts)
    {
        const script::Value args[] = {t.item, std::string("drop")};
        m_scripts->emit("telekinesis", args);
    }
    if (m_figure && m_figure->animator.state() == "mag_s_cast_loop" &&
        m_figure->animator.hasState("mag_t_cast_loop_end"))
    {
        m_figure->animator.enter("mag_t_cast_loop_end", 0.15f);
    }
}

void Engine::landTelekinesis()
{
    TelekinesisRun t = std::move(*m_telekinesis);
    m_telekinesis.reset();
    if (t.effect)
    {
        m_particles.stop(*t.effect);
    }
    // An item vob again where it landed (a runtime vob, so a save keeps it there), with its owner.
    const entt::entity e = m_scene.findById(t.vob);
    const world::ItemRef* ref = e != entt::null ? m_scene.get<world::ItemRef>(e) : nullptr;
    if (ref == nullptr)
    {
        return; // a script took it meanwhile
    }
    const world::ItemRef keep = *ref;
    const Vec3 landed = t.flight.position() - Vec3(0.0f, t.halfHeight, 0.0f);
    f32 yaw = t.flight.spin();
    if (const Transform* where = std::as_const(m_scene).get<Transform>(e); where != nullptr)
    {
        const Vec3 forward = where->rotation * Vec3(0.0f, 0.0f, -1.0f); // spawnItem: yaw about +Y from -Z
        yaw += std::atan2(-forward.x, -forward.z);
    }
    removeWorldItem(t.vob);
    auto spawned = spawnItem(keep.instance, keep.count, landed, yaw);
    if (!spawned)
    {
        G7_LOG_WARN("engine", "telekinesis: {} not put down: {}", keep.instance, spawned.error().message);
        return;
    }
    if (const entt::entity placed = m_scene.findById(spawned.value()); placed != entt::null)
    {
        m_scene.set<world::ItemRef>(placed, keep);
    }
}

void Engine::finishTelekinesis()
{
    if (!m_telekinesis)
    {
        return;
    }
    if (m_telekinesis->flight.phase() != gameplay::TelekinesisFlight::Phase::Falling)
    {
        dropTelekinesis();
    }
    m_telekinesis->flight.land();
    landTelekinesis();
}

void Engine::fixedUpdateTelekinesis(f32 seconds)
{
    if (!m_telekinesis)
    {
        return;
    }
    using Phase = gameplay::TelekinesisFlight::Phase;
    TelekinesisRun& t = *m_telekinesis;
    if (t.flight.phase() == Phase::Falling)
    {
        if (t.flight.fall(seconds) == Phase::Landed)
        {
            landTelekinesis();
            return;
        }
        placeTelekinesisItem();
        return;
    }
    const Vec3 hand = m_playerFeet + Vec3(0.0f, 1.4f, 0.0f) + gameplay::forwardOf(m_movement.yaw()) * 0.5f;
    const bool held = m_heroCast && m_heroCast->telekinesis && m_hero != nullptr && m_player.valid();
    const bool hit = m_hero != nullptr && m_hero->attribute("hp") < t.hp;
    const i32 due = gameplay::telekinesisManaDue(t.manaCarry, t.settings.manaPerSecond, seconds);
    const bool spent = m_hero == nullptr || m_hero->attribute("mana") < due;
    if (!held || hit || spent || t.flight.outOfReach(hand) || t.flight.stuck())
    {
        m_heroCast.reset();
        dropTelekinesis();
        placeTelekinesisItem();
        return;
    }
    (void)m_hero->setAttribute("mana", m_hero->attribute("mana") - due);
    const f32 radius = 0.12f;
    const auto sweep = [&](const Vec3& from, const Vec3& direction, f32 distance) -> std::optional<f32>
    {
        if (!m_physics.valid())
        {
            return std::nullopt;
        }
        const auto wall =
            m_physics.sphereCast(from, radius, direction, distance, physics::layerBit(physics::Layer::World));
        return wall ? std::optional<f32>(wall->distance) : std::nullopt;
    };
    const Phase before = t.flight.phase();
    const Phase now = t.flight.carry(seconds, hand, sweep);
    if (now == Phase::Arrived)
    {
        // At the hand: into the inventory like picking it up (theft included, decision 5).
        const world::VobId vob = t.vob;
        const std::string item = t.item;
        if (t.effect)
        {
            m_particles.stop(*t.effect);
        }
        m_telekinesis.reset();
        m_heroCast.reset();
        takeItem(vob);
        G7_LOG_INFO("engine", "telekinesis: {} arrives", item);
        if (m_scripts)
        {
            const script::Value args[] = {item, std::string("arrive")};
            m_scripts->emit("telekinesis", args);
        }
        if (m_figure && m_figure->animator.hasState("mag_t_cast_loop_end"))
        {
            m_figure->animator.enter("mag_t_cast_loop_end", 0.15f);
        }
        return;
    }
    if (before == Phase::Lift && now == Phase::Float && m_scripts)
    {
        const script::Value args[] = {t.item, std::string("float")};
        m_scripts->emit("telekinesis", args);
    }
    placeTelekinesisItem();
}

void Engine::bindTelekinesisFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"hero_telekinesis", "hero_telekinesis() -> {item, phase} | nil",
             "Der Gegenstand, den der Held gerade per Telekinese bewegt (M17 Teil T): Item und Phase "
             "(\"lift\", \"float\", \"drop\" = fällt); nil, wenn keiner.",
             "Magie", [this](std::span<const Value>) -> Result<Value>
             {
                 if (!m_telekinesis)
                 {
                     return Value();
                 }
                 return script::makeTable({},
                                          {{"item", m_telekinesis->item},
                                           {"phase", std::string(phaseName(m_telekinesis->flight.phase()))}});
             }});
    vm.bind({"telekinesis_allowed", "telekinesis_allowed(item: string) -> boolean",
             "Ob sich das Item per Telekinese bewegen lässt (M17 Teil T): nicht bei no_telekinesis (Story) "
             "und nicht, wenn es größer ist als 1,2 m (Entscheidung 4).",
             "Magie", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"expects (item)"};
                 }
                 const script::Instance* def = m_scripts->findInstance("Item", a[0].asString());
                 if (def == nullptr)
                 {
                     return Error{std::format("unknown item \"{}\"", a[0].asString())};
                 }
                 if (def->fields["no_telekinesis"].asBool())
                 {
                     return Value(false);
                 }
                 const LoadedModel* model = itemModel(a[0].asString());
                 const Vec3 size = model != nullptr ? model->bounds.max - model->bounds.min : Vec3(0.0f);
                 return Value(std::max({size.x, size.y, size.z}) <= gameplay::TelekinesisSettings{}.maxSize);
             }});
}
} // namespace g7

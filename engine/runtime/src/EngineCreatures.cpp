// Animals (M6 part D3): wolf, keiler and laufvogel as animated figures for tests and the debug UI - moved by
// the root motion of their clips, standing on the ground (a ray down, no capsule). Behaviour, collision and
// monster vobs in worlds come with M9.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
constexpr std::array<std::string_view, 3> kSpecies = {"wolf", "keiler", "laufvogel"};
constexpr usize kShownEvents = 6;

/// The showcase: every action in turn, for checking the clips (figuren).
struct ShowcaseStep
{
    std::string_view label;
    f32 seconds;
};
constexpr std::array<ShowcaseStep, 13> kShowcase = {{
    {"walk", 3.0f},
    {"run", 2.5f},
    {"stand", 1.0f},
    {"turn left", 1.5f},
    {"turn right", 1.5f},
    {"attack 1", 1.5f},
    {"attack 2", 1.5f},
    {"threaten", 2.0f},
    {"hit", 1.2f},
    {"eat", 3.0f},
    {"sleep", 3.5f},
    {"die", 3.0f},
    {"revive", 0.5f},
}};

Mat4 creatureMatrix(const Vec3& feet, f32 yaw)
{
    // Models face +Z, yaw 0 looks along -Z: half a turn more (as the player).
    return glm::translate(Mat4(1.0f), feet) * glm::rotate(Mat4(1.0f), yaw + glm::pi<f32>(), Vec3(0, 1, 0));
}
} // namespace

Creature* Engine::creature(u32 id) noexcept
{
    const auto it =
        std::find_if(m_creatures.begin(), m_creatures.end(), [&](const auto& c) { return c->id == id; });
    return it == m_creatures.end() ? nullptr : it->get();
}

const Creature* Engine::creature(u32 id) const noexcept
{
    const auto it =
        std::find_if(m_creatures.begin(), m_creatures.end(), [&](const auto& c) { return c->id == id; });
    return it == m_creatures.end() ? nullptr : it->get();
}

Result<u32> Engine::spawnCreature(std::string_view species, const Vec3& feet, f32 yaw)
{
    if (std::find(kSpecies.begin(), kSpecies.end(), species) == kSpecies.end())
    {
        return Error{std::format("unknown species '{}' (wolf, keiler, laufvogel)", species)};
    }
    return spawnAnimated(species, std::format("characters/monsters/{0}/rig/{0}_reference.glb", species),
                         std::format("data/anim/{}.animgraph.toml", species), feet, yaw);
}

Result<u32> Engine::spawnAnimated(std::string_view label, std::string_view model, std::string_view graph,
                                  const Vec3& feet, f32 yaw)
{
    auto c = std::make_unique<Creature>();
    c->species = std::string(label);
    c->figure = std::make_unique<AnimatedFigure>();
    auto loaded =
        loadAnimatedFigure(*c->figure, model, graph,
                           [&](const animation::AnimGraph& g, std::span<const asset::AnimationSetData* const>)
                           {
                               // The blend points of "move" (clip speeds): walk = the first forward one,
                               // run = the last, trot = one in between where there are three (wolf).
                               for (const animation::AnimGraphState& s : g.states)
                               {
                                   if (s.name != "move")
                                   {
                                       continue;
                                   }
                                   std::vector<f32> forward;
                                   for (const auto& point : s.points)
                                   {
                                       if (point.first > 0.0f)
                                       {
                                           forward.push_back(point.first);
                                       }
                                   }
                                   if (forward.size() >= 2)
                                   {
                                       c->walkSpeed = forward.front();
                                       c->runSpeed = forward.back();
                                       c->trotSpeed =
                                           forward.size() >= 3 ? forward[forward.size() - 2] : 0.0f;
                                   }
                               }
                           });
    if (!loaded)
    {
        return loaded.error();
    }
    c->id = m_nextCreatureId++;
    c->position = c->positionBefore = feet;
    c->yaw = c->yawBefore = yaw;
    G7_LOG_INFO("engine", "creature {} ({}) at ({:.1f}, {:.1f}, {:.1f})", c->id, label, feet.x, feet.y,
                feet.z);
    m_creatures.push_back(std::move(c));
    return m_creatures.back()->id;
}

void Engine::removeCreatures()
{
    m_creatures.clear();
}

void Engine::setCreatureMove(u32 id, f32 speed, f32 turn)
{
    if (Creature* c = creature(id))
    {
        c->speed = speed;
        c->turn = turn;
    }
}

bool Engine::creatureAction(u32 id, std::string_view action)
{
    Creature* c = creature(id);
    if (c == nullptr)
    {
        return false;
    }
    constexpr std::array<std::pair<std::string_view, i32>, 4> kOnce = {
        {{"attack_1", 1}, {"attack_2", 2}, {"hit", 3}, {"threaten", 4}}};
    for (const auto& [name, value] : kOnce)
    {
        if (action == name)
        {
            c->action = value;
            return true;
        }
    }
    if (action == "eat" || action == "sleep" || action == "stop")
    {
        c->eat = action == "eat";
        c->sleep = action == "sleep";
        return true;
    }
    if (action == "die")
    {
        c->dead = true;
        return true;
    }
    if (action == "revive")
    {
        c->dead = false;
        c->figure->animator.enter(c->figure->startState, 0.3f);
        return true;
    }
    return false;
}

void Engine::setCreatureShowcase(u32 id, bool on)
{
    if (Creature* c = creature(id))
    {
        c->showcase = on;
        c->showcaseStep = 0;
        c->showcaseTime = 0.0f;
        if (!on)
        {
            c->speed = c->turn = 0.0f;
            c->eat = c->sleep = false;
        }
    }
}

usize Engine::creatureCount() const noexcept
{
    return m_creatures.size();
}

std::string_view Engine::creatureState(u32 id) const noexcept
{
    const Creature* c = creature(id);
    return c ? c->figure->animator.state() : std::string_view();
}

std::optional<Vec3> Engine::creaturePosition(u32 id) const
{
    const Creature* c = creature(id);
    return c ? std::optional<Vec3>(c->position) : std::nullopt;
}

f32 Engine::creatureYaw(u32 id) const noexcept
{
    const Creature* c = creature(id);
    return c ? c->yaw : 0.0f;
}

void Engine::fixedUpdateCreatures(f32 seconds)
{
    // By index: a script state may insert NPCs while this runs (they move next step).
    const usize creatureCount = m_creatures.size();
    for (usize index = 0; index < creatureCount; ++index)
    {
        Creature& c = *m_creatures[index];
        if (c.vanished)
        {
            continue; // a summon that went (M12): kept only for the scripts' names
        }
        if (c.summoned)
        {
            // Z8: its time runs out; dead, it lies a moment and goes.
            c.summonSeconds = c.dead ? std::min(c.summonSeconds, 2.0f) : c.summonSeconds;
            c.summonSeconds -= seconds;
            if (c.summonSeconds <= 0.0f)
            {
                vanish(c);
                continue;
            }
        }
        AnimatedFigure& f = *c.figure;
        c.positionBefore = c.position;
        c.yawBefore = c.yaw;

        if (c.showcase)
        {
            // Start of a step: set its parameters; turns pulse "turn" once; eat/sleep end with their step.
            const auto label = kShowcase[c.showcaseStep].label;
            if (c.showcaseTime == 0.0f)
            {
                c.speed = label == "walk" ? c.walkSpeed : label == "run" ? c.runSpeed : 0.0f;
                c.turn = label == "turn left" ? -1.0f : label == "turn right" ? 1.0f : 0.0f;
                c.action = label == "attack 1"   ? 1
                           : label == "attack 2" ? 2
                           : label == "hit"      ? 3
                           : label == "threaten" ? 4
                                                 : 0;
                c.eat = label == "eat";
                c.sleep = label == "sleep";
                c.dead = label == "die";
                if (label == "revive")
                {
                    f.animator.enter(f.startState, 0.3f);
                }
            }
            else if (c.showcaseTime > 0.1f)
            {
                c.turn = 0.0f; // one turn per step
            }
            c.showcaseTime += seconds;
            if (c.showcaseTime >= kShowcase[c.showcaseStep].seconds)
            {
                c.showcaseStep = (c.showcaseStep + 1) % static_cast<u32>(kShowcase.size());
                c.showcaseTime = 0.0f;
            }
        }

        const gameplay::FightState fight = c.fighter.state();
        if (fight == gameplay::FightState::Down || fight == gameplay::FightState::Dead)
        {
            c.speed = 0.0f; // knocked out or dead (M11): no routine, no walking
        }
        else if (c.body || c.character)
        {
            fixedUpdateAi(c, seconds); // routine, state, commands (M9 part B)
            if (!c.simulated)
            {
                continue; // far from the player: neither walked nor animated (AI LOD)
            }
        }
        walkNpc(c, seconds); // human NPCs: their capsule along the route sets position, yaw and speed (M9)
        animation::Animator& a = f.animator;
        a.setFloat("speed", c.speed);
        a.setFloat("turn", c.turn);
        a.setFloat("action", static_cast<f32>(c.action));
        if (c.character && !c.dead)
        {
            a.setFloat("weapon", creatureWeaponAnimation(c)); // fighting: the stance of its weapon (M11)
        }
        a.setBool("eat", c.eat);
        a.setBool("sleep", c.sleep);
        a.setBool("dead", c.dead);
        a.update(seconds,
                 [&](std::string_view clip, std::string_view event)
                 {
                     handEvent(c, event); // broom, mug (M9 part B)
                     if (event.starts_with("sound:"))
                     {
                         (void)playSound(event.substr(6), c.position + Vec3(0.0f, 0.8f, 0.0f)); // M13
                     }
                     if ((event == "footstep_l" || event == "footstep_r") && c.body &&
                         nearListener(c.position))
                     {
                         footstep(c.position, c.speed); // M13 E: people (animals' steps follow)
                     }
                     if (event == "cast" && c.cast && !c.cast->acted)
                     {
                         applyNpcSpell(c); // the spell leaves its hand (M12 part D)
                     }
                     if (event.starts_with("hit_") || event.starts_with("combo_"))
                     {
                         c.fighter.onEvent(event); // the combat clips time the blow (M11)
                     }
                     f.events.push_front(std::format(
                         "{:.2f}  {}  {}", static_cast<f64>(m_simTicks) * m_fixedStep.step(), clip, event));
                     if (f.events.size() > kShownEvents)
                     {
                         f.events.pop_back();
                     }
                 });
        c.action = 0; // a trigger: one step

        // Root motion: turn first, then the movement in the turned frame (model -> world). NPCs with a
        // capsule move by it instead (their head and face below still move: M13 D found them still).
        if (!c.body)
        {
            c.yaw = std::remainder(c.yaw + a.rootMotionYaw(), 2.0f * glm::pi<f32>());
            const Vec3 ahead = Vec3(creatureMatrix(Vec3(0.0f), c.yaw) * Vec4(a.rootMotion(), 0.0f));
            c.position += Vec3(ahead.x, 0.0f, ahead.z);
            if (m_physics.valid())
            {
                // On the ground below (terrain and solid models); keeps its height over holes.
                const Vec3 from = c.position + Vec3(0.0f, 1.5f, 0.0f);
                if (const auto hit = m_physics.raycast(from, Vec3(0.0f, -1.0f, 0.0f), 30.0f,
                                                       physics::layerBit(physics::Layer::World)))
                {
                    c.position.y = hit->position.y;
                }
            }
        }
        f.posePrevious = std::move(f.poseNow);
        f.poseNow = a.pose();
        // Talking (M10): the head towards the player, the mouth moving; the face blinks always.
        if (f.lookAt.valid())
        {
            std::optional<Vec3> target;
            if (c.talking && m_player.valid())
            {
                const Vec3 head = m_player.feet() + Vec3(0.0f, 1.6f, 0.0f);
                target = Vec3(glm::inverse(creatureMatrix(c.position, c.yaw)) * Vec4(head, 1.0f));
            }
            f.lookAt.setTarget(target);
            f.lookAt.update(seconds, f.skeleton, f.poseNow);
        }
        f.face.update(seconds);
    }
    m_routineMinute = m_gameTime.totalMinutes(); // routines are checked once per game minute (M9 part B)
}

void Engine::drawCreatures(bool shadow, u32 cascade)
{
    const f32 alpha = static_cast<f32>(m_fixedStep.alpha());
    for (const auto& owned : m_creatures)
    {
        Creature& c = *owned;
        if (!c.figure->uploaded || c.vanished)
        {
            continue;
        }
        const f32 turn = std::remainder(c.yaw - c.yawBefore, 2.0f * glm::pi<f32>());
        const Mat4 transform =
            creatureMatrix(glm::mix(c.positionBefore, c.position, alpha), c.yawBefore + turn * alpha);
        drawAnimatedFigure(*c.figure, transform, shadow, cascade);
        if (c.handItem != nullptr && c.handBone < c.figure->modelSpace.size())
        {
            const Mat4 at = transform * c.figure->modelSpace[c.handBone];
            if (shadow)
            {
                m_meshRenderer.drawShadow(*m_device, c.handItem->mesh, c.handItem->materials, at,
                                          m_cascades[cascade]);
            }
            else
            {
                m_meshRenderer.draw(*m_device, c.handItem->mesh, c.handItem->materials, at, m_camera);
            }
        }
    }
}

void Engine::creaturesUi()
{
    ui::CreaturesPanel panel;
    for (const std::string_view s : kSpecies)
    {
        panel.species.emplace_back(s);
    }
    panel.speciesChoice = m_creatureSpecies;
    for (const auto& owned : m_creatures)
    {
        const Creature& c = *owned;
        ui::CreaturesPanel::Row row;
        row.id = c.id;
        row.label = std::format("{} {}", c.species, c.id);
        row.state = std::string(c.figure->animator.state());
        if (c.showcase)
        {
            row.state += std::format("  (showcase: {})", kShowcase[c.showcaseStep].label);
        }
        row.speed = c.speed;
        row.maxSpeed = std::max(c.runSpeed * 1.2f, 0.5f);
        row.showcase = c.showcase;
        row.events.assign(c.figure->events.begin(), c.figure->events.end());
        panel.rows.push_back(std::move(row));
    }
    m_debugUi.creaturesPanel(panel);

    m_creatureSpecies = panel.speciesChoice;
    if (panel.spawn)
    {
        // Five metres in front of the camera, on the ground, facing it.
        Vec3 ahead = m_camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
        ahead.y = 0.0f;
        ahead = glm::length(ahead) > 1e-3f ? glm::normalize(ahead) : Vec3(0.0f, 0.0f, -1.0f);
        Vec3 feet = m_camera.transform.position + ahead * 5.0f;
        if (m_physics.valid())
        {
            if (const auto hit = m_physics.raycast(feet + Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
                                                   100.0f, physics::layerBit(physics::Layer::World)))
            {
                feet.y = hit->position.y;
            }
        }
        const Vec3 toCamera = m_camera.transform.position - feet;
        if (auto spawned = spawnCreature(m_creatureSpecies, feet, std::atan2(-toCamera.x, -toCamera.z));
            !spawned)
        {
            G7_LOG_WARN("engine", "creature: {}", spawned.error().message);
        }
    }
    if (panel.removeAll)
    {
        removeCreatures();
        return;
    }
    for (const ui::CreaturesPanel::Row& row : panel.rows)
    {
        Creature* c = creature(row.id);
        if (c == nullptr)
        {
            continue;
        }
        if (row.showcase != c->showcase)
        {
            setCreatureShowcase(row.id, row.showcase);
        }
        if (!c->showcase)
        {
            c->speed = row.speed;
        }
        if (!row.action.empty())
        {
            if (row.action == "turn_l" || row.action == "turn_r")
            {
                c->turn = row.action == "turn_l" ? -1.0f : 1.0f;
                c->showcaseTime = 0.0f;
            }
            else
            {
                (void)creatureAction(row.id, row.action);
            }
        }
        else if (!c->showcase && c->turn != 0.0f && c->figure->animator.state() != "move")
        {
            c->turn = 0.0f; // a button turns once
        }
    }
}
} // namespace g7

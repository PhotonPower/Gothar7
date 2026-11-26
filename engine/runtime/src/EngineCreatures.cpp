// Animals (M6 part D3): wolf, keiler and laufvogel as animated figures for tests and the debug UI - moved by
// the root motion of their clips, standing on the ground (a ray down, no capsule). Behaviour, collision and
// monster vobs in worlds come with M9.

#include "PlayerFigure.hpp"

#include <g7/core/Crc32.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
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
constexpr f32 kAcrossTurnRate = 4.0f; ///< rad/s: a sideways walker turns its figure across its course

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
constexpr f32 kFigureHalfWidth =
    1.5f; ///< m: a figure's box for culling (people, wolves, boars; a little generous)
constexpr f32 kFigureHeight = 3.0f;

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
    // Every species with a graph (the Creatures window offers kSpecies; dungeon monsters come with D6).
    const std::string graph = std::format("data/anim/{}.animgraph.toml", species);
    if (species.empty() || !m_vfs.exists(graph))
    {
        return Error{std::format("unknown species '{}' (no {})", species, graph)};
    }
    return spawnAnimated(species, std::format("characters/monsters/{0}/rig/{0}_reference.glb", species),
                         graph, feet, yaw);
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
    constexpr std::array<std::pair<std::string_view, i32>, 16> kOnce = {{{"attack_1", 1},
                                                                         {"attack_2", 2},
                                                                         {"hit", 3},
                                                                         {"threaten", 4},
                                                                         {"wake", 5},
                                                                         {"steal", 6},
                                                                         {"grab", 7},
                                                                         {"reflect", 8},
                                                                         {"call", 9},
                                                                         {"rage", 10},
                                                                         {"stagger", 11},
                                                                         {"snap_up", 12},
                                                                         {"submerge", 13},
                                                                         {"listen", 14},
                                                                         {"scythe_l", 15},
                                                                         {"scythe_r", 16}}};
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
    G7_PROFILE_SCOPE("creatures (ai, animation)");
    updateSimulationLimit(); // AI LOD by count, before each creature decides by distance
    // Taken out by the count only, but visible and within the AI LOD distance: no statues (M17 perf part D) -
    // they keep playing their loop or idle, without AI, routes or perception.
    const Frustum view = m_camera.frustum();
    const Vec3 eye = m_player.valid() ? m_player.feet() : m_camera.transform.position;
    // By index: a script state may insert NPCs while this runs (they move next step).
    const usize creatureCount = m_creatures.size();
    m_animatedCreatures.clear();
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

        bool& animationOnly = c.animationOnly; // see above
        animationOnly = false;
        const gameplay::FightState fight = c.fighter.state();
        if (fight == gameplay::FightState::Down || fight == gameplay::FightState::Dead)
        {
            c.speed = 0.0f; // knocked out or dead (M11): no routine, no walking
        }
        else if (c.curledSeconds > 0.0f && !c.dead)
        {
            // Rolled up (Creatures.<species>.curl): it lies still under its shell, then goes on as before.
            c.curledSeconds = std::max(c.curledSeconds - seconds, 0.0f);
            c.speed = 0.0f;
            c.turn = 0.0f;
            c.action = 0;
        }
        else if (c.body || c.character)
        {
            fixedUpdateAi(c, seconds); // routine, state, commands (M9 part B)
            animationOnly = !c.simulated && !c.lodAllowed &&
                            glm::length(c.position - eye) < m_simulationDistance &&
                            view.intersects(Sphere{c.position + Vec3(0.0f, 0.9f, 0.0f), 1.2f});
            if (!c.simulated && !animationOnly)
            {
                continue; // far from the player: neither walked nor animated (AI LOD)
            }
        }
        if (animationOnly)
        {
            c.speed = 0.0f; // standing: its loop or idle plays on
            c.turn = 0.0f;
        }
        else
        {
            walkNpc(c,
                    seconds); // human NPCs: their capsule along the route sets position, yaw and speed (M9)
        }
        if (c.moveAcross)
        {
            // Dungeon D6 (move_axis = "x", characters-pipeline.md 7.5): its walk clips go along its left side
            // (+X), so while walking it turns its figure a quarter to the right of its course; standing,
            // fighting and dying it faces ahead again.
            const f32 want = c.speed > 0.2f && !c.dead ? -0.5f * glm::pi<f32>() : 0.0f;
            const f32 step = kAcrossTurnRate * seconds;
            c.acrossYaw += std::clamp(want - c.acrossYaw, -step, step);
        }
        animation::Animator& a = f.animator;
        a.setFloat("speed", c.speed);
        a.setFloat("turn", c.turn);
        a.setFloat("action", static_cast<f32>(c.action));
        if (c.character && !c.dead)
        {
            a.setFloat("weapon", creatureWeaponAnimation(c)); // fighting: the stance of its weapon (M11)
        }
        a.setBool("eat", c.eat);
        a.setBool("guard", c.guard); // Kellermutter over its treasure
        a.setBool("sleep", c.sleep);
        a.setBool("dormant", c.rest);                // dungeon D6: its resting loop (s_dormant)
        a.setBool("curled", c.curledSeconds > 0.0f); // rolled up after a heavy hit (Mauerkriecher)
        a.setBool("dead", c.dead);
        m_animatedCreatures.push_back(index);
    }

    // Phase 2 (M17 perf part C2), in parallel per figure: sample and blend its clips, look-at, face - each
    // job touches only its own figure; the clip events are collected (no Lua, no sounds, no random numbers
    // here).
    const std::optional<Vec3> heroHead =
        m_player.valid() ? std::optional<Vec3>(m_player.feet() + Vec3(0.0f, 1.6f, 0.0f)) : std::nullopt;
    {
        G7_PROFILE_SCOPE("creatures: animation (parallel)");
        m_jobs->parallelFor(m_animatedCreatures.size(), 0,
                            [&](usize begin, usize end)
                            {
                                for (usize k = begin; k < end; ++k)
                                {
                                    Creature& c = *m_creatures[m_animatedCreatures[k]];
                                    AnimatedFigure& f = *c.figure;
                                    c.pendingEvents.clear();
                                    f.animator.update(seconds,
                                                      [&c](std::string_view clip, std::string_view event)
                                                      { c.pendingEvents.emplace_back(clip, event); });
                                    f.posePrevious = std::move(f.poseNow);
                                    f.poseNow = f.animator.pose();
                                    // Talking (M10): the head towards the player, the mouth moving; the face
                                    // blinks always (its own random numbers, per figure).
                                    if (f.lookAt.valid())
                                    {
                                        std::optional<Vec3> target;
                                        if (c.talking && heroHead)
                                        {
                                            target = Vec3(glm::inverse(creatureMatrix(c.position, c.yaw)) *
                                                          Vec4(*heroHead, 1.0f));
                                        }
                                        f.lookAt.setTarget(target);
                                        f.lookAt.update(seconds, f.skeleton, f.poseNow);
                                    }
                                    f.face.update(seconds);
                                }
                            });
    }

    // Phase 3, serial in creature order: the clip events, then root motion.
    for (const usize index : m_animatedCreatures)
    {
        Creature& c = *m_creatures[index];
        for (const auto& [clip, event] : c.pendingEvents)
        {
            creatureClipEvent(c, clip, event);
        }
        c.pendingEvents.clear();
        c.action = 0; // a trigger: one step

        // Root motion: turn first, then the movement in the turned frame (model -> world). NPCs with a
        // capsule move by it instead (their head and face still move: M13 D found them still).
        const animation::Animator& a = c.figure->animator;
        if (!c.body && !c.animationOnly)
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
    }
    m_routineMinute = m_gameTime.totalMinutes(); // routines are checked once per game minute (M9 part B)
}

void Engine::creatureClipEvent(Creature& c, std::string_view clip, std::string_view event)
{
    handEvent(c, event); // broom, mug (M9 part B)
    if (event.starts_with("sound:"))
    {
        (void)playSound(event.substr(6), c.position + Vec3(0.0f, 0.8f, 0.0f)); // M13
    }
    if ((event == "footstep_l" || event == "footstep_r") && c.body && nearListener(c.position))
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
    if (event == "flash")
    {
        creatureFlash(c); // Beinhauswart: the lantern flares
    }
    if (event == "grab")
    {
        creatureGrab(c); // Kristallkrebs: the pincer closes
    }
    else if (event == "release" && m_heroHeld && m_heroHeld->by == c.id)
    {
        releaseHero("release");
    }
    else if (event == "reflect")
    {
        throwSpellBack(c); // Kristallkrebs: its crystals flare
    }
    creatureScriptEvent(c, event); // steal, emerge, grab ... (dungeon D6)
    AnimatedFigure& f = *c.figure;
    f.events.push_front(
        std::format("{:.2f}  {}  {}", static_cast<f64>(m_simTicks) * m_fixedStep.step(), clip, event));
    if (f.events.size() > kShownEvents)
    {
        f.events.pop_back();
    }
}

void Engine::prepareCreaturePoses(bool shadows)
{
    G7_PROFILE_SCOPE("render::figure poses");
    const f32 alpha = static_cast<f32>(m_fixedStep.alpha());
    const Frustum view = m_camera.frustum();
    const Vec3 eye = m_camera.transform.position;
    // The reaches of drawCreatures: in view (main pass), or near enough to cast a shadow into some cascade.
    const auto reachOf = [](f32 limit, f32 frame) { return frame > 0.0f ? std::min(limit, frame) : limit; };
    const f32 viewReach = reachOf(m_figureCull.distance, m_frameCull.viewDistance);
    const f32 shadowReach = reachOf(m_figureCull.shadowDistance, m_frameShadowCull.viewDistance);
    m_poseCreatures.clear();
    for (const auto& owned : m_creatures)
    {
        Creature& c = *owned;
        if (!c.figure->uploaded || c.vanished || c.figure->drawnFrame == m_frameCount)
        {
            continue;
        }
        const AABB box{c.position - Vec3(kFigureHalfWidth, 0.0f, kFigureHalfWidth),
                       c.position + Vec3(kFigureHalfWidth, kFigureHeight, kFigureHalfWidth)};
        const Vec3 nearest = glm::clamp(eye, box.min, box.max);
        if (render::figureInView(box, eye, view, viewReach) ||
            (shadows && glm::length(nearest - eye) <= shadowReach))
        {
            m_poseCreatures.push_back(&c);
        }
    }
    m_jobs->parallelFor(m_poseCreatures.size(), 0,
                        [&](usize begin, usize end)
                        {
                            for (usize k = begin; k < end; ++k)
                            {
                                computeFigurePose(*m_poseCreatures[k]->figure, alpha, m_frameCount);
                            }
                        });
    for (Creature* c : m_poseCreatures)
    {
        c->figure->mesh.setMorphWeights(c->figure->face.weights()); // GL: on this thread
    }
}

u32 Engine::creatureDigest() const
{
    u32 crc = 0;
    const auto add = [&crc](const void* data, usize size)
    { crc = crc32(std::span(static_cast<const u8*>(data), size), crc); };
    const auto addText = [&add](std::string_view text)
    {
        add(text.data(), text.size());
        add("|", 1);
    };
    for (const auto& owned : m_creatures)
    {
        const Creature& c = *owned;
        const AnimatedFigure& f = *c.figure;
        add(&c.id, sizeof(c.id));
        add(&c.position, sizeof(c.position));
        add(&c.yaw, sizeof(c.yaw));
        add(&c.speed, sizeof(c.speed));
        add(&c.turn, sizeof(c.turn));
        const u8 flags[] = {c.dead, c.simulated, c.animationOnly, c.vanished};
        add(flags, sizeof(flags));
        addText(f.animator.state());
        add(f.poseNow.data(), f.poseNow.size() * sizeof(animation::BoneTransform));
        add(f.posePrevious.data(), f.posePrevious.size() * sizeof(animation::BoneTransform));
        add(f.face.weights().data(), f.face.weights().size_bytes());
        const auto fight = static_cast<u32>(c.fighter.state());
        add(&fight, sizeof(fight));
        if (c.character)
        {
            const i32 hp = c.character->attribute("hp");
            add(&hp, sizeof(hp));
        }
        for (const std::string& event : f.events)
        {
            addText(event);
        }
    }
    return crc;
}

void Engine::drawCreatures(bool shadow, u32 cascade)
{
    G7_PROFILE_SCOPE("render::figures");
    const f32 alpha = static_cast<f32>(m_fixedStep.alpha());
    // Only figures near and in view (main pass) or in the cascade's light volume and nearer (shadows).
    const Frustum volume =
        shadow ? Frustum::fromViewProjection(m_cascades[cascade].viewProjection) : m_camera.frustum();
    // Indoors not beyond the room's view distance either (m_frameCull, render.md "Innenräume").
    const f32 limit = shadow ? m_figureCull.shadowDistance : m_figureCull.distance;
    const f32 frame = shadow ? m_frameShadowCull.viewDistance : m_frameCull.viewDistance;
    const f32 reach = frame > 0.0f ? std::min(limit, frame) : limit;
    for (const auto& owned : m_creatures)
    {
        Creature& c = *owned;
        if (!c.figure->uploaded || c.vanished)
        {
            continue;
        }
        const AABB box{c.position - Vec3(kFigureHalfWidth, 0.0f, kFigureHalfWidth),
                       c.position + Vec3(kFigureHalfWidth, kFigureHeight, kFigureHalfWidth)};
        if (!render::figureInView(box, m_camera.transform.position, volume, reach))
        {
            continue;
        }
        const f32 turn = std::remainder(c.yaw - c.yawBefore, 2.0f * glm::pi<f32>());
        const Mat4 transform = creatureMatrix(glm::mix(c.positionBefore, c.position, alpha),
                                              c.yawBefore + turn * alpha + c.acrossYaw);
        drawAnimatedFigure(*c.figure, transform, shadow, cascade,
                           shadow ? render::FigureGlow{} : creatureGlow(c));
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
    if (ghostFigureShown()) // the editor's routine ghost (M16 part E)
    {
        drawAnimatedFigure(*m_ghostFigure->figure, creatureMatrix(m_ghostFigure->feet, m_ghostFigure->yaw),
                           shadow, cascade);
    }
}

std::vector<render::PointLight> Engine::creatureLights() const
{
    // Creatures.<species>.glow: the crab's crystals, the mother's weak spot, the warden's lantern. Dead ones
    // go dark; a light that needs something in hand goes out when it is dropped.
    std::vector<render::PointLight> out;
    std::vector<Mat4> space;
    for (const auto& owned : m_creatures)
    {
        const Creature& c = *owned;
        if (c.glow.empty() || c.dead || c.vanished || !c.figure)
        {
            continue;
        }
        const auto it = m_glowRules.find(c.glow);
        if (it == m_glowRules.end() || (it->second.holding && c.heldItem.empty()))
        {
            continue;
        }
        const i32 bone = c.figure->skeleton.find(it->second.socket);
        if (bone < 0)
        {
            continue;
        }
        // The pose of this step (the drawn modelSpace is not there without a window or out of view).
        space.resize(c.figure->skeleton.size());
        c.figure->skeleton.modelSpace(c.figure->poseNow, space);
        const Mat4 at = creatureMatrix(c.position, c.yaw + c.acrossYaw) * space[static_cast<usize>(bone)];
        out.push_back(
            render::PointLight{Vec3(at[3]), it->second.radius, it->second.colour, it->second.intensity});
    }
    return out;
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

bool Engine::setGhostFigure(std::optional<GhostFigure> ghost)
{
    if (!ghost)
    {
        if (m_ghostFigure)
        {
            m_ghostFigure->shown = false;
        }
        return true;
    }
    if (!m_ghostFigure)
    {
        m_ghostFigure = std::make_unique<GhostFigureState>();
    }
    GhostFigureState& g = *m_ghostFigure;
    if (g.npc != ghost->npc || !g.figure)
    {
        // The NPC's figure as spawnNpc chooses it (animals: their species' model and graph), at rest.
        g.figure.reset();
        g.npc = ghost->npc;
        const script::Instance* npc = m_scripts ? m_scripts->findInstance("Npc", ghost->npc) : nullptr;
        if (npc == nullptr)
        {
            g.shown = false;
            return false;
        }
        const std::string species = npc->fields["species"].isString()
                                        ? std::string(npc->fields["species"].asString())
                                        : std::string();
        const std::string model = species.empty()
                                      ? npcFigurePath(*npc, ghost->npc)
                                      : std::format("characters/monsters/{0}/rig/{0}_reference.glb", species);
        const std::string graph = species.empty() ? std::string("data/anim/human.animgraph.toml")
                                                  : std::format("data/anim/{}.animgraph.toml", species);
        auto figure = std::make_unique<AnimatedFigure>();
        if (auto loaded = loadAnimatedFigure(*figure, model, graph); !loaded)
        {
            G7_LOG_WARN("engine", "ghost of {}: {}", ghost->npc, loaded.error().message);
            g.shown = false;
            return false;
        }
        g.figure = std::move(figure);
    }
    g.feet = ghost->feet;
    g.yaw = ghost->yaw;
    g.shown = true;
    return true;
}

bool Engine::ghostFigureShown() const noexcept
{
    return m_ghostFigure && m_ghostFigure->shown && m_ghostFigure->figure && m_ghostFigure->figure->uploaded;
}

namespace
{
template <typename Load>
Result<std::vector<Engine::ModelMarker>> markersOf(const asset::Vfs& vfs, std::string_view path, Load load)
{
    std::vector<Engine::ModelMarker> out;
    if (!path.ends_with(".glb"))
    {
        return out; // cooked meshes keep no empty nodes and no extras
    }
    auto bytes = vfs.read(path);
    if (!bytes)
    {
        return bytes.error();
    }
    auto markers = load(std::span<const u8>(bytes.value()));
    if (!markers)
    {
        return markers.error();
    }
    for (asset::GltfMarker& m : markers.value())
    {
        out.push_back({std::move(m.name), m.world, std::move(m.extras)});
    }
    return out;
}
} // namespace

Result<std::vector<Engine::ModelMarker>> Engine::modelMarkers(std::string_view path,
                                                              std::string_view prefix) const
{
    return markersOf(m_vfs, path,
                     [&](std::span<const u8> bytes) { return asset::loadGltfMarkers(bytes, prefix, path); });
}

Result<std::vector<Engine::ModelMarker>> Engine::modelMeshExtras(std::string_view path,
                                                                 std::string_view key) const
{
    return markersOf(m_vfs, path,
                     [&](std::span<const u8> bytes) { return asset::loadGltfMeshExtras(bytes, key, path); });
}
} // namespace g7

namespace g7
{
usize Engine::simulatedCreatureCount() const noexcept
{
    return static_cast<usize>(std::count_if(m_creatures.begin(), m_creatures.end(),
                                            [](const auto& c) { return !c->vanished && c->simulated; }));
}
} // namespace g7

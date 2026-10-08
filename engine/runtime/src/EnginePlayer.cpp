// The player (M5 part C): a character on the start point, moved by the input actions with the values of
// data/movement.toml, and the third-person camera behind it. The animated figure: EngineFigure.cpp (M6).

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/StartPoints.hpp>

#include <cmath>
#include <format>
#include <string>

namespace g7
{
namespace
{
constexpr std::string_view kPlayerModel = "characters/figures/placeholder_mannequin.glb";

f32 yawOf(const Quat& rotation)
{
    const Vec3 forward = rotation * Vec3(0.0f, 0.0f, -1.0f);
    return std::atan2(-forward.x, -forward.z);
}

physics::CharacterDesc characterDesc(const gameplay::MovementSettings& s)
{
    physics::CharacterDesc desc; // size: human capsule (physics.md), agreed with figuren
    desc.maxSlopeDegrees = s.maxSlopeDegrees;
    desc.stepHeight = s.stepHeight;
    desc.stickToFloor = s.stickToFloor;
    return desc;
}
} // namespace

void Engine::initPlayer()
{
    m_assets->registerLoader<gameplay::MovementSettings>(
        [](const asset::LoadContext& ctx) -> Result<gameplay::MovementSettings>
        {
            return gameplay::MovementSettings::parse(
                std::string_view(reinterpret_cast<const char*>(ctx.bytes.data()), ctx.bytes.size()),
                ctx.path);
        });
    const std::string path = m_config.settings.get<std::string>("game.movement", "data/movement.toml");
    m_movementData = m_assets->load<gameplay::MovementSettings>(path);
    m_assets->waitAll();
    m_assets->update();
    refreshMovementSettings();
    if (m_movementData.failed())
    {
        G7_LOG_WARN("engine", "movement data {}: {} - built-in defaults", path, m_movementData.error());
    }
    loadFocusSettings(); // data/focus.toml (M8)
    loadMobTypes();      // data/mobs.toml (M8 part C)
}

void Engine::refreshMovementSettings()
{
    if (!m_movementData.isReady() || m_movementData.version() == m_movementVersion)
    {
        return;
    }
    m_movementVersion = m_movementData.version();
    m_movementSettings = *m_movementData.get();
    if (m_player.valid())
    {
        m_player.setLimits(m_movementSettings.maxSlopeDegrees, m_movementSettings.stepHeight,
                           m_movementSettings.stickToFloor);
    }
    G7_LOG_INFO("engine", "movement: run {} m/s, walk {}, sneak {}; step {} m, slope {} deg; camera {} m",
                m_movementSettings.runSpeed, m_movementSettings.walkSpeed, m_movementSettings.sneakSpeed,
                m_movementSettings.stepHeight, m_movementSettings.maxSlopeDegrees,
                m_movementSettings.camera.distance);
}

void Engine::spawnPlayer()
{
    removePlayer();
    if (!m_config.player || m_config.benchmark || !m_physics.valid())
    {
        return;
    }
    auto start = world::findStartPoint(m_scene, m_config.start.empty() ? std::string_view() : m_config.start);
    if (!start)
    {
        // Wrong names were reported by applyStartPoint; a world without start point keeps the free camera.
        start = world::findStartPoint(m_scene, {});
    }
    if (!start)
    {
        G7_LOG_INFO("engine", "no start point: free camera, no player");
        return;
    }
    const Mat4 placement = m_scene.worldMatrix(start.value());
    const Vec3 feet = Vec3(placement[3]) + Vec3(0.0f, 0.05f, 0.0f); // a little above, it settles
    auto character = physics::CharacterController::create(m_physics, characterDesc(m_movementSettings), feet);
    if (!character)
    {
        G7_LOG_WARN("engine", "no player: {}", character.error().message);
        return;
    }
    m_player = std::move(character).value();
    m_swimmer.reset(m_movementSettings.swim);
    m_drownDamage = 0.0f;
    m_drownLogged = 0.0f;
    const f32 yaw =
        yawOf(glm::quat_cast(Mat3(glm::normalize(Vec3(placement[0])), glm::normalize(Vec3(placement[1])),
                                  glm::normalize(Vec3(placement[2])))));
    m_movement.reset(yaw);
    m_playerFeet = m_playerFeetBefore = m_player.visualFeet();
    m_playerYawBefore = yaw;
    m_playerCamera.reset(m_playerFeet, yaw, m_movementSettings.camera);
    m_flyMode = false;
    loadPlayerFigure();
    resetPlayerAnimation();
    if (!m_figure && m_playerModel == nullptr && m_device)
    {
        if (auto loaded = loadModels({std::string(kPlayerModel)}); loaded)
        {
            m_playerModel = model(kPlayerModel);
        }
        else
        {
            G7_LOG_WARN("engine", "player figure: {} - the debug overlay (F2) shows the capsule",
                        loaded.error().message);
        }
    }
    G7_LOG_INFO("engine", "player at {} ({:.1f}, {:.1f}, {:.1f}), facing {:.0f} deg",
                m_scene.get<world::Vob>(start.value())->nameText, feet.x, feet.y, feet.z, glm::degrees(yaw));
}

Vec3 Engine::triggerProbePosition() const
{
    // Triggers notice the player at hip height, or the camera without a player.
    return m_player.valid() ? m_player.feet() + Vec3(0.0f, 0.5f * m_player.desc().height, 0.0f)
                            : m_camera.transform.position;
}

void Engine::teleportPlayer(const Vec3& feet, f32 yaw)
{
    if (!m_player.valid())
    {
        return;
    }
    m_climb.reset();
    m_player.teleport(feet);
    m_movement.reset(yaw);
    m_swimmer.reset(m_movementSettings.swim); // decided again at the next step
    m_playerFeet = m_playerFeetBefore = m_player.visualFeet();
    m_playerCamera.reset(m_playerFeet, yaw, m_movementSettings.camera);
    resetPlayerAnimation();
}

void Engine::steerPlayer(f32 yaw)
{
    m_movement.setYaw(yaw);
}

void Engine::removePlayer()
{
    if (m_transform)
    {
        // Leaving the world in an animal's shape: human again (the new world starts with him).
        m_movementSettings = m_transform->humanMovement;
        m_figure = std::move(m_transform->human);
        m_transform.reset();
        m_weaponMode = 0;
    }
    m_player = {};
    m_climb.reset();
    m_jumpCooldown = 0.0f;
    if (m_playerMouse && m_window)
    {
        m_window->setRelativeMouse(false);
    }
    m_playerMouse = false;
}

void Engine::updatePlayerInput(bool allowMouse, bool allowKeyboard)
{
    using platform::Action;
    if (!m_player.valid())
    {
        return;
    }
    // (debug_fly switches to the free camera: Engine::setFlyMode, EngineView.cpp)
    // The player camera captures the mouse while the game runs and nothing else wants it.
    const bool wantMouse =
        !m_flyMode && !m_paused && allowMouse && !m_debugUiVisible && !m_consoleOpen && !m_inventoryOpen &&
        !(m_mobUse && (m_mobUse->phase == MobUse::Phase::Picklock || m_mobUse->phase == MobUse::Phase::Loop));
    if (wantMouse != m_playerMouse && m_window)
    {
        m_playerMouse = wantMouse && m_window->setRelativeMouse(true);
        if (!wantMouse)
        {
            m_window->setRelativeMouse(false);
        }
    }
    const auto axis = [&](Action positive, Action negative)
    {
        if (!allowKeyboard || m_flyMode)
        {
            return 0.0f;
        }
        return (m_actions.isDown(m_input, positive) ? 1.0f : 0.0f) -
               (m_actions.isDown(m_input, negative) ? 1.0f : 0.0f);
    };
    // Keys: their state now; mouse: summed up until the next fixed step uses it.
    m_playerInput.forward = axis(Action::MoveForward, Action::MoveBack);
    m_playerInput.strafe = axis(Action::StrafeRight, Action::StrafeLeft);
    m_playerInput.turn = axis(Action::TurnRight, Action::TurnLeft);
    m_playerInput.walk = allowKeyboard && !m_flyMode && m_actions.isDown(m_input, Action::Walk);
    m_playerInput.sneak = allowKeyboard && !m_flyMode && m_actions.isDown(m_input, Action::Sneak);
    if (allowKeyboard && !m_flyMode && m_actions.pressed(m_input, Action::Jump))
    {
        m_playerInput.jump = true; // until the next fixed step uses it
    }
    m_playerInput.jumpHeld = allowKeyboard && !m_flyMode && m_actions.isDown(m_input, Action::Jump);
    if (allowKeyboard && !m_flyMode && m_actions.pressed(m_input, Action::DrawWeapon))
    {
        m_drawWeaponRequested = true; // until the next fixed step uses it (M9 part C)
    }
    if (allowKeyboard && !m_flyMode && m_actions.pressed(m_input, Action::DrawRanged))
    {
        m_drawRangedRequested = true; // M11 (R1)
    }
    if (allowKeyboard && !m_flyMode && m_actions.pressed(m_input, Action::DrawMagic))
    {
        m_drawMagicRequested = true; // M12 (Z4)
    }
    for (u32 i = 0; i < 6 && allowKeyboard && !m_flyMode; ++i)
    {
        if (m_actions.pressed(m_input, static_cast<Action>(static_cast<u16>(Action::Rune1) + i)))
        {
            m_runeRequested = i; // Z4: a rune place
        }
    }
    // Z5: with magic drawn the fighting keys held charge, released cast.
    m_castHeld =
        allowKeyboard && !m_flyMode && m_weaponMode == 4 &&
        ((m_actions.isDown(m_input, Action::Action) && m_actions.isDown(m_input, Action::MoveForward)) ||
         m_actions.isDown(m_input, Action::Attack));
    if (allowKeyboard && !m_flyMode && m_weaponMode != 0)
    {
        readCombatInput(); // M11 (K1): fighting keys instead of moving while the action key is held
    }
    if (m_playerMouse)
    {
        m_playerInput.mouseTurn += m_input.mouseDelta().x;
        m_playerPitchPixels += m_input.mouseDelta().y;
    }
}

void Engine::fixedUpdatePlayer(f32 seconds)
{
    if (!m_player.valid())
    {
        return;
    }
    // Z7: the shape changes here, between steps.
    if (std::exchange(m_transformBackRequested, false))
    {
        endTransform();
    }
    if (const std::string species = std::exchange(m_transformRequested, std::string()); !species.empty())
    {
        (void)beginTransform(species);
    }
    gameplay::MoveInput input = m_playerInputOverride.value_or(m_playerInput);
    if (m_playerInputOverride)
    {
        input.jump = m_playerInputOverride->jump && !m_overrideJumped; // once per switching on
        m_overrideJumped = m_playerInputOverride->jump;
    }
    if (m_pickup || m_inventoryOpen || m_mobUse || m_itemUse || m_pickpocket)
    {
        input = {}; // the hero stands while picking something up or looking into his bag (Gothic)
    }
    fixedUpdateHeroFight(input, seconds); // M11: the fighting moves and the target lock
    fixedUpdateHeroMagic(input, seconds); // M12: charging and casting
    m_playerInput.mouseTurn = 0.0f;       // used up by this step
    m_playerInput.jump = false;
    m_playerYawBefore = m_movement.yaw();
    m_playerFeetBefore = m_playerFeet;
    movePlayer(seconds, input);
    animatePlayer(seconds, input);
}

void Engine::movePlayer(f32 seconds, const gameplay::MoveInput& input)
{
    const gameplay::MovementSettings& s = m_movementSettings;

    if (m_mobUse)
    {
        // At a mob (M8 part C): walking to its slot, then held there without collision - the clips are made
        // for the slot, and the mob's own collision must not push the hero off it.
        if (m_mobUse->phase == MobUse::Phase::Approach)
        {
            approachMob(seconds);
        }
        else
        {
            m_player.moveTo(m_mobUse->place.feet);
        }
        m_playerFeet = m_player.feet();
        return;
    }
    if (m_climb)
    {
        // With the climb clip until it stands on top (root motion, else the path); input waits.
        m_climbSeconds += seconds;
        if (m_climbSeconds >= m_climb->seconds)
        {
            m_player.teleport(m_climb->to);
            m_climb.reset();
        }
        else
        {
            m_player.moveTo(climbPosition());
        }
        m_playerFeet = m_climb ? m_player.feet() : m_player.visualFeet();
        return;
    }

    // Water: swimming at the surface or diving, from hip deep water on (gameplay.md).
    const Vec3 feetNow = m_player.feet();
    const auto surface = m_water.surfaceAt(feetNow);
    const gameplay::WaterMode before = m_swimmer.mode();
    const gameplay::Swimmer::Step swim =
        m_swimmer.step(seconds, feetNow.y, surface, m_movement.yaw(), input, s.swim);
    if (swim.drownDamage > 0.0f)
    {
        m_drownDamage += swim.drownDamage;
        if (m_drownDamage >= m_drownLogged + 10.0f)
        {
            m_drownLogged = std::floor(m_drownDamage);
            G7_LOG_INFO("engine", "drowning: {:.0f} hit points lost", m_drownDamage);
        }
    }
    if (swim.mode != gameplay::WaterMode::Land)
    {
        if (before == gameplay::WaterMode::Land)
        {
            m_movement.stop();
            if (const auto fall = m_player.takeLanding()) // the water catches a fall
            {
                m_lastLanding = PlayerLanding{*fall, 0.0f, true, feetNow, m_simTicks};
            }
        }
        gameplay::MoveInput turnOnly; // turning as on land; the swimmer gives the velocity
        turnOnly.turn = input.turn;
        turnOnly.mouseTurn = input.mouseTurn;
        m_movement.step(turnOnly, seconds, s);
        if (input.jump && swim.mode == gameplay::WaterMode::Swim)
        {
            // Out of the water: a bank or ledge in front is climbed (from the feet, deep below).
            const auto ledge = m_player.findLedge(gameplay::forwardOf(m_movement.yaw()), s.stepHeight,
                                                  s.climb.highMax, s.climb.reach);
            const auto kind = ledge ? gameplay::classifyLedge(ledge->height, s.climb) : std::nullopt;
            if (ledge && kind)
            {
                startClimb(feetNow, ledge->feet, *kind);
                m_swimmer.reset(s.swim);
                m_playerFeet = feetNow;
                return;
            }
        }
        m_player.swim(seconds, swim.velocity);
        m_playerFeet = m_player.feet(); // floating: the cylinder's bottom is the body's lowest point
        return;
    }

    const bool running = m_movement.running(s);
    const Vec3 velocity = m_movement.step(input, seconds, s);
    m_jumpCooldown = std::max(0.0f, m_jumpCooldown - seconds);
    if (input.jump && m_jumpCooldown <= 0.0f && m_player.state() == physics::MoveState::Ground)
    {
        // In front of a ledge within reach the jump key climbs (Gothic); else it jumps.
        const auto ledge = m_player.findLedge(gameplay::forwardOf(m_movement.yaw()), s.stepHeight,
                                              s.climb.highMax, s.climb.reach);
        const auto kind = ledge ? gameplay::classifyLedge(ledge->height, s.climb) : std::nullopt;
        if (ledge && kind && !m_transform) // an animal does not climb (Z7)
        {
            startClimb(m_player.feet(), ledge->feet, *kind);
            m_movement.stop();
            G7_LOG_DEBUG("engine", "climb {:.2f} m ({})", ledge->height,
                         *kind == gameplay::LedgeClass::Low   ? "low"
                         : *kind == gameplay::LedgeClass::Mid ? "mid"
                                                              : "high");
            m_playerFeet = m_player.feet();
            return;
        }
        m_player.jump(gameplay::jumpSpeed(running ? s.jump.runHeight : s.jump.standHeight));
        if (m_figure)
        {
            m_figure->jumped = true;
        }
    }
    m_player.update(seconds, velocity);
    if (const auto fall = m_player.takeLanding())
    {
        m_jumpCooldown = s.jump.cooldown;
        const Vec3 landed = m_player.feet();
        const auto pool = m_water.surfaceAt(landed);
        const bool intoWater = pool && landed.y < *pool; // water catches any fall
        const f32 damage = intoWater ? 0.0f : gameplay::fallDamage(*fall, s.fall);
        m_lastLanding = PlayerLanding{*fall, damage, intoWater, landed, m_simTicks};
        if (damage > 0.0f)
        {
            m_lastFallDamage = damage;
            // Hit points come with M8; until then the damage is only reported.
            G7_LOG_INFO("engine", "fall damage {:.0f} (fell {:.1f} m)", damage, *fall);
        }
    }
    m_playerFeet = m_player.visualFeet();
}

void Engine::updatePlayerCamera(f64 realSeconds)
{
    if (!playerCameraActive())
    {
        return;
    }
    const f32 alpha = static_cast<f32>(m_fixedStep.alpha());
    const Vec3 feet = glm::mix(m_playerFeetBefore, m_playerFeet, alpha);
    const auto obstruction = [this](const Vec3& from, const Vec3& direction, f32 radius,
                                    f32 maxDistance) -> std::optional<f32>
    {
        const auto hit = m_physics.sphereCast(from, radius, direction, maxDistance,
                                              physics::layerBit(physics::Layer::World));
        return hit ? std::optional<f32>(hit->distance) : std::nullopt;
    };
    // Inside (a roof above the hero, welt's walkable houses): the closer indoor camera, blended in and out. A
    // roof above the hero and around him (4 of 5 points, 1 m apart): walking along a house under its jettied
    // upper floor (half a metre over the street) is still outside.
    const gameplay::IndoorCameraSettings& indoor = m_movementSettings.indoor;
    int covered = 0;
    if (m_physics.valid())
    {
        for (const Vec2 offset :
             {Vec2(0.0f), Vec2(1.0f, 0.0f), Vec2(-1.0f, 0.0f), Vec2(0.0f, 1.0f), Vec2(0.0f, -1.0f)})
        {
            covered += m_physics
                               .raycast(feet + Vec3(offset.x, 1.0f, offset.y), Vec3(0.0f, 1.0f, 0.0f),
                                        std::max(indoor.ceiling - 1.0f, 0.1f),
                                        physics::layerBit(physics::Layer::World))
                               .has_value()
                           ? 1
                           : 0;
        }
    }
    // In a room of the world file (zones of type indoor) it stays inside even without the roof hits: on a
    // stair under the open ceiling the slanted roof may be higher than the probes reach (welt, W7 stairs).
    const Vec3 body = feet + Vec3(0.0f, 0.9f, 0.0f);
    const bool roof = covered >= 4 || render::indoorAmount(body, nearestIndoorVolumes(body), 0.0f) > 0.5f;
    const f32 towards = indoor.blendSeconds > 0.0f
                            ? 1.0f - std::exp(-static_cast<f32>(realSeconds) / indoor.blendSeconds)
                            : 1.0f;
    m_indoorBlend += ((roof ? 1.0f : 0.0f) - m_indoorBlend) * towards;
    // Fighting (M11, K5): weapon drawn and an enemy locked - the combat profile; indoors the nearer one.
    const bool fighting = m_weaponMode != 0 && m_combatTarget.has_value();
    const f32 combatTowards =
        m_movementSettings.combatBlendSeconds > 0.0f
            ? 1.0f - std::exp(-static_cast<f32>(realSeconds) / m_movementSettings.combatBlendSeconds)
            : 1.0f;
    m_combatBlend += ((fighting ? 1.0f : 0.0f) - m_combatBlend) * combatTowards;
    const gameplay::CameraSettings around =
        gameplay::blendCamera(m_movementSettings.camera, indoor.camera, m_indoorBlend);
    gameplay::CameraSettings combat = m_movementSettings.combat;
    combat.distance = std::min(combat.distance, around.distance);
    combat.minDistance = std::min(combat.minDistance, combat.distance);
    combat.targetHeight = std::min(combat.targetHeight, around.targetHeight);
    combat.collisionRadius = around.collisionRadius;
    m_playerCamera.update(static_cast<f32>(realSeconds), feet, m_movement.yaw(), m_playerPitchPixels,
                          gameplay::blendCamera(around, combat, m_combatBlend), obstruction);
    m_playerPitchPixels = 0.0f;
    Vec3 eye = m_playerCamera.position();
    // Above the water while swimming: no under-water view until M17.
    if (const auto surface = m_water.surfaceAt(eye, 0.3f);
        surface && eye.y < *surface + 0.3f && m_swimmer.mode() != gameplay::WaterMode::Dive)
    {
        eye.y = *surface + 0.3f;
    }
    if (m_dialog)
    {
        // Talking (M10 part B): shot and reverse shot instead of the camera behind the hero. The player
        // camera keeps following, so that it is where it belongs when the dialogue ends.
        if (!m_dialogCamera)
        {
            m_camera.transform.position = eye;
            m_camera.transform.rotation = m_playerCamera.rotation();
        }
        updateDialogCamera(static_cast<f32>(realSeconds));
        return;
    }
    m_camera.transform.position = eye;
    m_camera.transform.rotation = m_playerCamera.rotation();
}

void Engine::drawPlayer(bool shadow, u32 cascade)
{
    if (!m_player.valid())
    {
        return;
    }
    const Mat4 transform = playerFigureTransform();
    if (drawPlayerFigure(transform, shadow, cascade) || m_playerModel == nullptr)
    {
        return;
    }
    if (shadow)
    {
        m_meshRenderer.drawShadow(*m_device, m_playerModel->mesh, m_playerModel->materials, transform,
                                  m_cascades[cascade]);
    }
    else
    {
        m_meshRenderer.draw(*m_device, m_playerModel->mesh, m_playerModel->materials, transform, m_camera);
    }
}

void Engine::drawPlayerDebug()
{
    if (!m_player.valid())
    {
        return;
    }
    // The collision cylinder (rings and four lines), the ground normal, the state and speed.
    const physics::CharacterDesc& d = m_player.desc();
    const Vec3 bottom = m_player.feet();
    const Vec3 top = bottom + Vec3(0.0f, d.height, 0.0f);
    const physics::MoveState state = m_player.state();
    const Vec4 colour = state == physics::MoveState::Ground  ? Vec4(0.3f, 1.0f, 0.4f, 1.0f)
                        : state == physics::MoveState::Slide ? Vec4(1.0f, 0.6f, 0.1f, 1.0f)
                                                             : Vec4(0.4f, 0.6f, 1.0f, 1.0f);
    const render::DebugStyle style{colour};
    m_debugDraw.circle(bottom, Vec3(0, 1, 0), d.radius, style);
    m_debugDraw.circle(top, Vec3(0, 1, 0), d.radius, style);
    for (const Vec3& side : {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 0, 1), Vec3(0, 0, -1)})
    {
        m_debugDraw.line(bottom + side * d.radius, top + side * d.radius, style);
    }
    m_debugDraw.arrow(bottom, bottom + m_player.groundNormal() * 0.6f, style);
    m_debugDraw.arrow(top, top + gameplay::forwardOf(m_movement.yaw()) * 0.8f, style);
    const Vec3 v = m_player.velocity();
    if (m_climb)
    {
        m_debugDraw.arrow(m_climb->from, Vec3(m_climb->from.x, m_climb->to.y, m_climb->from.z), style);
        m_debugDraw.arrow(Vec3(m_climb->from.x, m_climb->to.y, m_climb->from.z), m_climb->to, style);
    }
    for (const world::WaterBody& body : m_water.bodies())
    {
        const Mat4 box =
            glm::translate(Mat4(1.0f), body.centre) * glm::rotate(Mat4(1.0f), body.yaw, Vec3(0, 1, 0));
        m_debugDraw.box(box, body.halfExtents, {Vec4(0.3f, 0.6f, 1.0f, 1.0f)});
    }
    if (m_swimmer.mode() != gameplay::WaterMode::Land)
    {
        m_debugDraw.text(top + Vec3(0.0f, 0.6f, 0.0f),
                         std::format("{} air {:.0f} s",
                                     m_swimmer.mode() == gameplay::WaterMode::Dive ? "dive" : "swim",
                                     m_swimmer.airSeconds()),
                         style);
    }
    const char* name = m_climb                               ? "climb"
                       : state == physics::MoveState::Ground ? "ground"
                       : state == physics::MoveState::Slide  ? "slide"
                                                             : "air";
    m_debugDraw.text(top + Vec3(0.0f, 0.3f, 0.0f),
                     std::format("{} {:.1f} m/s", name, std::sqrt(v.x * v.x + v.z * v.z)), style);
}
} // namespace g7

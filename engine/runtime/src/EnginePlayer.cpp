// The player (M5 part C): a character on the start point, moved by the input actions with the values of
// data/movement.toml, and the third-person camera behind it. Until animation (M6) the placeholder
// figure is drawn as a static model.

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
    const f32 yaw =
        yawOf(glm::quat_cast(Mat3(glm::normalize(Vec3(placement[0])), glm::normalize(Vec3(placement[1])),
                                  glm::normalize(Vec3(placement[2])))));
    m_movement.reset(yaw);
    m_playerFeet = m_playerFeetBefore = m_player.visualFeet();
    m_playerYawBefore = yaw;
    m_playerCamera.reset(m_playerFeet, yaw, m_movementSettings.camera);
    m_flyMode = false;
    if (m_playerModel == nullptr && m_device)
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

void Engine::removePlayer()
{
    m_player = {};
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
    if (allowKeyboard && m_actions.pressed(m_input, Action::DebugFly))
    {
        m_flyMode = !m_flyMode;
        if (m_flyMode)
        {
            m_flyCamera.attach(m_camera);
        }
        G7_LOG_INFO("engine", "{}",
                    m_flyMode ? "free debug camera (F3: back to the player)" : "player camera");
    }
    // The player camera captures the mouse while the game runs and nothing else wants it.
    const bool wantMouse = !m_flyMode && !m_paused && allowMouse && !m_debugUiVisible;
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
    const gameplay::MoveInput input = m_playerInputOverride.value_or(m_playerInput);
    m_playerInput.mouseTurn = 0.0f; // used up by this step
    m_playerYawBefore = m_movement.yaw();
    const Vec3 velocity = m_movement.step(input, seconds, m_movementSettings);
    m_player.update(seconds, velocity);
    m_playerFeetBefore = m_playerFeet;
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
    m_playerCamera.update(static_cast<f32>(realSeconds), feet, m_movement.yaw(), m_playerPitchPixels,
                          m_movementSettings.camera, obstruction);
    m_playerPitchPixels = 0.0f;
    m_camera.transform.position = m_playerCamera.position();
    m_camera.transform.rotation = m_playerCamera.rotation();
}

void Engine::drawPlayer(bool shadow, u32 cascade)
{
    if (!m_player.valid() || m_playerModel == nullptr)
    {
        return;
    }
    const f32 alpha = static_cast<f32>(m_fixedStep.alpha());
    const Vec3 feet = glm::mix(m_playerFeetBefore, m_playerFeet, alpha);
    // Models face +Z, yaw 0 looks along -Z: half a turn more.
    const f32 yaw = m_movement.yaw();
    const Mat4 transform =
        glm::translate(Mat4(1.0f), feet) * glm::rotate(Mat4(1.0f), yaw + glm::pi<f32>(), Vec3(0, 1, 0));
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
    const char* name = state == physics::MoveState::Ground  ? "ground"
                       : state == physics::MoveState::Slide ? "slide"
                                                            : "air";
    m_debugDraw.text(top + Vec3(0.0f, 0.3f, 0.0f),
                     std::format("{} {:.1f} m/s", name, std::sqrt(v.x * v.x + v.z * v.z)), style);
}
} // namespace g7

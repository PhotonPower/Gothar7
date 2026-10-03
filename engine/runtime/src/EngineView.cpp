// The free camera and reproducible views (development): fly mode (F3 / --fly) with its own keys, mouse look
// and wheel speed; the start options --cam/--yaw/--pitch/--fly/--player; copy_position (F6) puts the view on
// the clipboard as those options; short notices at the bottom of the screen.

#include <g7/core/Log.hpp>
#include <g7/platform/Clipboard.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <initializer_list>
#include <vector>

namespace g7
{
namespace
{
constexpr f32 kMinFlySpeed = 0.5f;
constexpr f32 kMaxFlySpeed = 500.0f;
constexpr f32 kWheelStep = 1.25f; // speed factor per wheel step
constexpr f64 kHintSeconds = 6.0;
constexpr f64 kNoticeSeconds = 2.5;
} // namespace

void Engine::showNotice(std::string text, f64 seconds)
{
    m_notice = std::move(text);
    m_noticeUntil = m_realTime + seconds;
}

bool Engine::noticeVisible() const noexcept
{
    return !m_notice.empty() && m_realTime < m_noticeUntil;
}

void Engine::drawNotice(u32 height)
{
    if (noticeVisible())
    {
        // Bottom left, above the edge; a line is about 16 px at scale 1.5.
        const auto lines = static_cast<f32>(1 + std::count(m_notice.begin(), m_notice.end(), '\n'));
        m_debugDraw.screenText(Vec2(8.0f, static_cast<f32>(height) - 20.0f * lines - 12.0f), m_notice,
                               Vec4(1.0f, 0.9f, 0.5f, 1.0f), 1.5f);
    }
}

std::string flyHintText(const platform::ActionMap& actions, f32 speed)
{
    using platform::Action;
    // The first input of each action; parts whose actions are all unbound are left out (no "-" in the hint).
    const auto key = [&](Action action)
    {
        const auto bindings = actions.bindings(action);
        return bindings.empty() ? std::string() : std::string(platform::name(bindings.front()));
    };
    const auto join = [](std::initializer_list<std::string> keys, std::string_view separator)
    {
        std::string out;
        for (const std::string& k : keys)
        {
            if (!k.empty())
            {
                out += (out.empty() ? "" : std::string(separator)) + k;
            }
        }
        return out;
    };
    std::vector<std::string> parts;
    const auto add = [&](const std::string& keys, std::string_view what)
    {
        if (!keys.empty())
        {
            parts.push_back(std::format("{} {}", keys, what));
        }
    };
    // W/A/S/D-like single letters read as one word ("WASD"), longer names with slashes.
    const std::string forward = key(Action::FlyForward);
    const std::string left = key(Action::FlyLeft);
    const std::string back = key(Action::FlyBack);
    const std::string right = key(Action::FlyRight);
    const bool letters = forward.size() == 1 && left.size() == 1 && back.size() == 1 && right.size() == 1;
    add(letters ? forward + left + back + right : join({forward, left, back, right}, "/"), "move");
    add(join({key(Action::FlyUp), key(Action::FlyDown)}, "/"), "up/down");
    add(key(Action::FlyFast), "fast");
    std::string line1 = "fly:";
    for (const std::string& p : parts)
    {
        line1 += (line1 == "fly:" ? " " : ", ") + p;
    }
    line1 += std::format("{}mouse look, wheel speed ({:.0f} m/s)", parts.empty() ? " " : ", ", speed);
    std::vector<std::string> extra;
    if (const std::string copy = key(Action::CopyPosition); !copy.empty())
    {
        extra.push_back(copy + " copy position");
    }
    if (const std::string back3 = key(Action::DebugFly); !back3.empty())
    {
        extra.push_back(back3 + " back");
    }
    std::string line2;
    for (const std::string& e : extra)
    {
        line2 += (line2.empty() ? "" : ", ") + e;
    }
    return line2.empty() ? line1 : line1 + "\n" + line2;
}

void Engine::setFlyMode(bool on)
{
    if (on == m_flyMode)
    {
        return;
    }
    m_flyMode = on;
    if (m_mouseLook && m_window)
    {
        m_window->setRelativeMouse(false); // fly mode captures again in updateDebugCamera
    }
    m_mouseLook = false;
    if (on)
    {
        m_flyCamera.attach(m_camera);
        showNotice(flyHintText(m_actions, m_flyCamera.speed), kHintSeconds);
    }
    G7_LOG_INFO("engine", "{}", on ? "fly mode (free camera)" : "player camera");
}

void Engine::updateDebugCamera(f64 realSeconds, bool allowMouse, bool allowKeyboard)
{
    using platform::Action;
    const auto axis = [&](Action positive, Action negative)
    {
        if (!allowKeyboard)
        {
            return 0.0f;
        }
        return (m_actions.isDown(m_input, positive) ? 1.0f : 0.0f) -
               (m_actions.isDown(m_input, negative) ? 1.0f : 0.0f);
    };

    if (m_flyMode)
    {
        // Fly mode: the mouse looks around all the time, unless the debug UI is open.
        const bool capture = !m_debugUiVisible && !m_consoleOpen;
        if (capture != m_mouseLook && m_window)
        {
            m_mouseLook = capture && m_window->setRelativeMouse(true);
            if (!capture)
            {
                m_window->setRelativeMouse(false);
            }
        }
    }
    // Elsewhere (editor, worlds without a player) while the right mouse button is held. It starts only
    // outside the debug UI but, once started, continues over it.
    else if (allowMouse && m_input.pressed(platform::MouseButton::Right))
    {
        m_mouseLook = m_window->setRelativeMouse(true);
    }
    else if (m_mouseLook && !m_input.isDown(platform::MouseButton::Right))
    {
        m_window->setRelativeMouse(false);
        m_mouseLook = false;
    }

    if (allowMouse && m_input.wheelDelta() != 0.0f)
    {
        m_flyCamera.speed = std::clamp(m_flyCamera.speed * std::pow(kWheelStep, m_input.wheelDelta()),
                                       kMinFlySpeed, kMaxFlySpeed);
        showNotice(std::format("fly speed {:.1f} m/s", m_flyCamera.speed), kNoticeSeconds);
    }

    render::FreeFlyInput fly;
    fly.move = Vec3(axis(Action::FlyRight, Action::FlyLeft), axis(Action::FlyUp, Action::FlyDown),
                    axis(Action::FlyForward, Action::FlyBack));
    fly.lookDelta = m_mouseLook ? m_input.mouseDelta() : Vec2(0.0f);
    fly.fast = allowKeyboard && m_actions.isDown(m_input, Action::FlyFast);
    // Real time: the debug camera keeps working while the game is paused or slowed down.
    m_flyCamera.update(m_camera, fly, realSeconds);
}

void Engine::applyStartView()
{
    const StartView& v = m_config.view;
    if (v.empty())
    {
        return;
    }
    const bool cameraView = v.camera.has_value() || v.fly;
    if (v.player)
    {
        if (m_player.valid())
        {
            const f32 yaw = !cameraView && v.yawDegrees ? glm::radians(*v.yawDegrees) : m_movement.yaw();
            teleportPlayer(*v.player, yaw);
        }
        else
        {
            G7_LOG_WARN("engine", "--player: no player in this world");
        }
    }
    if (cameraView)
    {
        if (v.camera)
        {
            m_camera.transform.position = *v.camera;
        }
        m_flyCamera.attach(m_camera);
        m_flyCamera.setOrientation(m_camera, v.yawDegrees ? glm::radians(*v.yawDegrees) : m_flyCamera.yaw(),
                                   v.pitchDegrees ? glm::radians(*v.pitchDegrees) : m_flyCamera.pitch());
        // With a player the free camera needs fly mode (else the player camera takes over again).
        if (v.fly || m_player.valid())
        {
            setFlyMode(true); // takes yaw and pitch from the camera just turned
        }
    }
    G7_LOG_INFO("engine", "start view: {}", viewLine());
}

std::string Engine::viewLine() const
{
    ViewLine line;
    line.world = m_worldPath;
    line.fly = !playerCameraActive();
    line.minuteOfDay = static_cast<u32>(m_gameTime.minuteOfDay());
    render::FreeFlyCamera probe; // yaw and pitch of the camera as it is now
    probe.attach(m_camera);
    if (line.fly)
    {
        line.position = m_camera.transform.position;
        line.yawDegrees = glm::degrees(probe.yaw());
        line.pitchDegrees = glm::degrees(probe.pitch());
    }
    else
    {
        line.position = m_player.feet();
        line.yawDegrees = glm::degrees(m_movement.yaw());
        line.pitchDegrees = glm::degrees(probe.pitch());
    }
    return formatViewLine(line);
}

void Engine::copyViewToClipboard()
{
    const std::string line = viewLine();
    G7_LOG_INFO("engine", "position: {}", line);
    if (auto copied = platform::setClipboardText(line); !copied)
    {
        G7_LOG_WARN("engine", "{}", copied.error().message);
        showNotice("position in the log (clipboard failed)", kNoticeSeconds);
        return;
    }
    showNotice("position copied", kNoticeSeconds);
}
} // namespace g7

#include <g7/ai/Ai.hpp>
#include <g7/animation/Animation.hpp>
#include <g7/asset/Asset.hpp>
#include <g7/audio/Audio.hpp>
#include <g7/core/Clock.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/core/Version.hpp>
#include <g7/gameplay/Gameplay.hpp>
#include <g7/physics/Physics.hpp>
#include <g7/platform/Platform.hpp>
#include <g7/platform/Time.hpp>
#include <g7/render/Render.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/save/Save.hpp>
#include <g7/script/Script.hpp>
#include <g7/ui/Ui.hpp>
#include <g7/world/World.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace g7
{
namespace
{
/// Shows which actions fire (--verbose); fulfils the M1 check "log output of the actions".
void logPressedActions(const platform::ActionMap& actions, const platform::Input& input)
{
    if (log::minLevel() > log::Level::Debug)
    {
        return;
    }
    for (usize i = 0; i < static_cast<usize>(platform::Action::Count); ++i)
    {
        const auto action = static_cast<platform::Action>(i);
        if (actions.pressed(input, action))
        {
            G7_LOG_DEBUG("engine", "action {}", platform::name(action));
        }
    }
}
} // namespace

Engine::Engine(EngineConfig config) : m_config(std::move(config))
{
}

Engine::~Engine()
{
    shutdown();
}

Result<void> Engine::init()
{
    G7_LOG_INFO("engine", "{} {}.{}.{} - starting '{}'", kEngineName, kEngineVersion.major,
                kEngineVersion.minor, kEngineVersion.patch, m_config.appName);

    if (m_config.simulationHz <= 0.0)
    {
        return Error{"simulationHz must be > 0"};
    }

    // Initialization order = dependency order (docs/02-architecture.md).
    // TODO(M1+): replace with real subsystem init calls as modules get implemented.
    const std::array<std::string_view, 12> modules = {
        platform::moduleName(), asset::moduleName(),     render::moduleName(), audio::moduleName(),
        physics::moduleName(),  animation::moduleName(), world::moduleName(),  script::moduleName(),
        ai::moduleName(),       gameplay::moduleName(),  ui::moduleName(),     save::moduleName()};
    for (const auto name : modules)
    {
        G7_LOG_DEBUG("engine", "module '{}' registered (stub)", name);
    }

    if (!m_config.headless)
    {
        auto window = platform::Window::create(m_config.window);
        if (!window)
        {
            return Error{"cannot create window: " + window.error().message};
        }
        m_window = std::move(window).value();
    }
    else
    {
        G7_LOG_INFO("engine", "headless mode (no window)");
    }

    if (m_window)
    {
        const std::string scheme = m_config.settings.get<std::string>("input.scheme", "classic");
        m_actions = platform::ActionMap::fromConfig(m_config.settings, scheme);
        m_input.setStickDeadzone(static_cast<f32>(m_config.settings.get<f64>("input.stick_deadzone", 0.2)));
        G7_LOG_INFO("engine", "control scheme '{}'", scheme);
    }

    m_fixedStep = FixedStep(1.0 / m_config.simulationHz);
    m_framePacer = FramePacer(m_config.maxFps);
    m_frameTimer.reset();
    if (m_window && m_config.maxFps > 0.0)
    {
        G7_LOG_INFO("engine", "frame rate capped at {} fps", m_config.maxFps);
    }

    m_initialized = true;
    return {};
}

int Engine::run()
{
    if (!m_initialized)
    {
        G7_LOG_ERROR("engine", "run() called before successful init()");
        return 1;
    }
    while (runFrame())
    {
    }
    G7_LOG_INFO("engine", "main loop finished after {} frames / {} ticks", m_frameCount, m_simTicks);
    return 0;
}

bool Engine::runFrame()
{
    if (!m_initialized || m_quitRequested)
    {
        return false;
    }
    G7_PROFILE_FRAME();
    G7_PROFILE_SCOPE("Engine::frame");
    if (m_window)
    {
        m_framePacer.frameStarted(platform::nowSeconds());
    }
    const f64 realSeconds =
        m_config.fixedFrameSeconds > 0.0 ? m_config.fixedFrameSeconds : m_frameTimer.elapsedSeconds();
    m_frameTimer.reset();

    m_input.beginFrame();
    if (m_window)
    {
        if (!m_window->pollEvents(m_input))
        {
            G7_LOG_INFO("engine", "quit requested by window");
            requestQuit();
        }
        else if (m_window->resizedSinceLastPoll())
        {
            const auto size = m_window->pixelSize();
            G7_LOG_DEBUG("engine", "window resized to {}x{} px", size.width, size.height);
        }
        logPressedActions(m_actions, m_input);

        // Interim until the menu exists (M14): the pause action toggles the pause directly.
        if (m_actions.pressed(m_input, platform::Action::Pause))
        {
            setPaused(!m_paused);
        }
    }

    // While paused the accumulator is not fed, so nothing is caught up afterwards.
    const u32 steps = m_paused ? 0 : m_fixedStep.advance(realSeconds * m_timeScale);
    for (u32 i = 0; i < steps; ++i)
    {
        G7_PROFILE_SCOPE("Engine::fixedUpdate");
        // TODO(M4+): world/ai/gameplay/physics fixed update
        ++m_simTicks;
    }
    {
        G7_PROFILE_SCOPE("Engine::render");
        // TODO(M2): render.drawFrame(frameAlpha()) + swap (VSync from WindowDesc::vsync)
    }

    ++m_frameCount;
    if (m_config.maxFrames != 0 && m_frameCount >= m_config.maxFrames)
    {
        requestQuit();
    }

    if (m_window && !m_quitRequested)
    {
        G7_PROFILE_SCOPE("Engine::frameCap");
        platform::sleepPrecise(m_framePacer.secondsUntilNextFrame(platform::nowSeconds()));
    }
    return !m_quitRequested;
}

void Engine::setTimeScale(f64 scale) noexcept
{
    m_timeScale = std::clamp(scale, 0.0, kMaxTimeScale);
}

void Engine::setPaused(bool paused) noexcept
{
    if (paused != m_paused)
    {
        m_paused = paused;
        G7_LOG_INFO("engine", "{}", paused ? "paused" : "resumed");
    }
}

void Engine::shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    // Shutdown in reverse init order.
    G7_LOG_INFO("engine", "shutdown");
    m_window.reset();
    m_initialized = false;
}
} // namespace g7

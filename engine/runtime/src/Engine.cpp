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
#include <g7/render/Render.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/save/Save.hpp>
#include <g7/script/Script.hpp>
#include <g7/ui/Ui.hpp>
#include <g7/world/World.hpp>

#include <array>
#include <string_view>
#include <utility>

namespace g7
{
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

    FixedStep fixedStep(1.0 / m_config.simulationHz);
    Stopwatch frameTimer;

    while (!m_quitRequested)
    {
        G7_PROFILE_FRAME();
        G7_PROFILE_SCOPE("Engine::frame");
        const f64 frameSeconds = frameTimer.elapsedSeconds();
        frameTimer.reset();

        // TODO(M1): input actions from events
        if (m_window)
        {
            if (!m_window->pollEvents())
            {
                G7_LOG_INFO("engine", "quit requested by window");
                requestQuit();
            }
            else if (m_window->resizedSinceLastPoll())
            {
                const auto size = m_window->pixelSize();
                G7_LOG_DEBUG("engine", "window resized to {}x{} px", size.width, size.height);
            }
        }

        const u32 steps = fixedStep.advance(frameSeconds);
        for (u32 i = 0; i < steps; ++i)
        {
            G7_PROFILE_SCOPE("Engine::fixedUpdate");
            // TODO(M4+): world/ai/gameplay/physics fixed update
            ++m_simTicks;
        }
        {
            G7_PROFILE_SCOPE("Engine::render");
            // TODO(M2): render.drawFrame(fixedStep.alpha())
        }

        ++m_frameCount;
        if (m_config.maxFrames != 0 && m_frameCount >= m_config.maxFrames)
        {
            requestQuit();
        }
    }

    G7_LOG_INFO("engine", "main loop finished after {} frames / {} ticks", m_frameCount, m_simTicks);
    return 0;
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

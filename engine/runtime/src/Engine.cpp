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
/// Dusk colour until there is a sky (M4).
const Vec4 kClearColor{0.10f, 0.11f, 0.14f, 1.0f};

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
        platform::WindowDesc desc = m_config.window;
        desc.graphics = m_config.render ? platform::GraphicsApi::OpenGL : platform::GraphicsApi::None;
        auto window = platform::Window::create(desc);
        if (!window)
        {
            return Error{"cannot create window: " + window.error().message};
        }
        m_window = std::move(window).value();

        if (m_config.render)
        {
            auto context = platform::GlContext::create(*m_window);
            if (!context)
            {
                return Error{"cannot create OpenGL context: " + context.error().message};
            }
            m_glContext = std::move(context).value();
            auto device =
                render::Device::create(&platform::GlContext::procAddress, platform::GlContextDesc{}.debug);
            if (!device)
            {
                return Error{"cannot initialize renderer: " + device.error().message};
            }
            m_device = std::move(device).value();
            m_glContext->setVSync(m_config.window.vsync);

            if (auto result = initShaders(); !result)
            {
                return result;
            }
        }
        else
        {
            G7_LOG_INFO("engine", "rendering disabled (--no-render)");
        }
    }
    else
    {
        G7_LOG_INFO("engine", "headless mode (no window)");
    }

    // Camera from [camera] (defaults: 70° vertical FOV, 0.1–1500 m, 0.1°/px mouse).
    m_camera.fovY = toRadians(static_cast<f32>(m_config.settings.get<f64>("camera.fov", 70.0)));
    m_camera.nearPlane = static_cast<f32>(m_config.settings.get<f64>("camera.near", 0.1));
    m_camera.farPlane = static_cast<f32>(m_config.settings.get<f64>("camera.far", 1500.0));
    m_camera.transform.position = Vec3(0.0f, 1.8f, 6.0f);
    m_flyCamera.sensitivity =
        toRadians(static_cast<f32>(m_config.settings.get<f64>("camera.mouse_sensitivity", 0.1)));
    m_flyCamera.speed = static_cast<f32>(m_config.settings.get<f64>("camera.fly_speed", 10.0));
    m_flyCamera.attach(m_camera);

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
        updateDebugCamera(realSeconds);

        // Interim until the menu exists (M14): the pause action toggles the pause directly.
        if (m_actions.pressed(m_input, platform::Action::Pause))
        {
            setPaused(!m_paused);
        }
    }

    // While paused the accumulator is not fed, so nothing is caught up afterwards.
    const u32 steps = m_paused ? 0 : m_fixedStep.advance(realSeconds * m_timeScale);
    if (m_fixedStep.droppedSeconds() > 0.0)
    {
        // E.g. window dragged on Windows (modal loop), debugger break, loading stall.
        G7_LOG_DEBUG("engine", "frame hitch: {:.0f} ms, {:.0f} ms simulation time dropped",
                     realSeconds * 1000.0, m_fixedStep.droppedSeconds() * 1000.0);
    }
    for (u32 i = 0; i < steps; ++i)
    {
        G7_PROFILE_SCOPE("Engine::fixedUpdate");
        // TODO(M4+): world/ai/gameplay/physics fixed update
        ++m_simTicks;
    }
    {
        G7_PROFILE_SCOPE("Engine::render");
        if (m_device)
        {
            // TODO(M2): scene rendering with frameAlpha(); for now the frame is only cleared.
            const auto size = m_window->pixelSize();
            m_shaders->update(platform::nowSeconds());
            m_device->beginFrame(size.width, size.height, kClearColor);
            m_camera.aspect =
                size.height > 0 ? static_cast<f32>(size.width) / static_cast<f32>(size.height) : 1.0f;
            m_backgroundProgram->setUniform("uInverseViewProjection",
                                            glm::inverse(m_camera.viewProjection()));
            m_backgroundProgram->setUniform("uCameraPosition", m_camera.transform.position);
            m_device->bindPipeline(m_backgroundPipeline);
            m_device->draw(3); // fullscreen triangle from gl_VertexID
            m_glContext->swapBuffers();
        }
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

Result<void> Engine::initShaders()
{
    const fs::Path root =
        m_config.shaderDirectory.empty() ? fs::gamePath("shaders") : m_config.shaderDirectory;
    m_shaders = std::make_unique<render::ShaderLibrary>(*m_device, root);
#ifdef NDEBUG
    constexpr bool kHotReloadDefault = false;
#else
    constexpr bool kHotReloadDefault = true;
#endif
    m_shaders->setHotReload(m_config.settings.get<bool>("render.shader_hot_reload", kHotReloadDefault));
    G7_LOG_INFO("engine", "shaders from {}{}", fs::toUtf8(root),
                m_shaders->hotReload() ? " (hot-reload)" : "");

    auto background = m_shaders->load("background", {"background.vert", "background.frag", {}});
    if (!background)
    {
        return Error{"cannot load shaders: " + background.error().message};
    }
    render::rhi::PipelineDesc desc;
    m_backgroundProgram = background.value();
    desc.program = m_backgroundProgram;
    desc.cull = render::rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    auto pipeline = m_device->createPipeline(desc);
    if (!pipeline)
    {
        return Error{"cannot create background pipeline: " + pipeline.error().message};
    }
    m_backgroundPipeline = std::move(pipeline).value();
    return {};
}

void Engine::updateDebugCamera(f64 realSeconds)
{
    using platform::Action;
    const auto axis = [&](Action positive, Action negative)
    {
        return (m_actions.isDown(m_input, positive) ? 1.0f : 0.0f) -
               (m_actions.isDown(m_input, negative) ? 1.0f : 0.0f);
    };

    // Mouse look while the right mouse button is held (relative mode hides and captures the cursor).
    if (m_input.pressed(platform::MouseButton::Right))
    {
        m_mouseLook = m_window->setRelativeMouse(true);
    }
    else if (m_mouseLook && !m_input.isDown(platform::MouseButton::Right))
    {
        m_window->setRelativeMouse(false);
        m_mouseLook = false;
    }

    render::FreeFlyInput fly;
    fly.move = Vec3(axis(Action::StrafeRight, Action::StrafeLeft), axis(Action::Jump, Action::Sneak),
                    axis(Action::MoveForward, Action::MoveBack));
    fly.turn = axis(Action::TurnLeft, Action::TurnRight);
    fly.lookDelta = m_mouseLook ? m_input.mouseDelta() : Vec2(0.0f);
    fly.fast = m_actions.isDown(m_input, Action::Run);
    // Real time: the debug camera keeps working while the game is paused or slowed down.
    m_flyCamera.update(m_camera, fly, realSeconds);
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
    m_backgroundPipeline = {};
    m_backgroundProgram = nullptr;
    m_shaders.reset();
    m_device.reset(); // GL objects need the context
    m_glContext.reset();
    m_window.reset();
    m_initialized = false;
}
} // namespace g7

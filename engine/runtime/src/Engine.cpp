#include <g7/ai/Ai.hpp>
#include <g7/animation/Animation.hpp>
#include <g7/asset/Asset.hpp>
#include <g7/asset/Procedural.hpp>
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

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <format>
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
            if (!m_config.viewMesh.empty())
            {
                if (auto result = initViewMesh(); !result)
                {
                    return result;
                }
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
    m_frameSeconds = realSeconds;
    m_smoothedFrameSeconds =
        m_smoothedFrameSeconds > 0.0 ? m_smoothedFrameSeconds * 0.95 + realSeconds * 0.05 : realSeconds;

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
        if (m_actions.pressed(m_input, platform::Action::DebugDraw))
        {
            setDebugOverlay(!m_debugOverlay);
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
            renderScene(size.width, size.height);
            m_glContext->swapBuffers();
        }
        // Real time, so timed debug items also expire while the game is paused.
        m_debugDraw.advance(static_cast<f32>(m_frameSeconds));
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

    auto post = render::PostProcess::create(*m_device, *m_shaders);
    if (!post)
    {
        return Error{"cannot create post pass: " + post.error().message};
    }
    m_post = std::move(post).value();
    auto debugRenderer = render::DebugDrawRenderer::create(*m_device, *m_shaders);
    if (!debugRenderer)
    {
        return Error{"cannot create debug draw: " + debugRenderer.error().message};
    }
    m_debugRenderer = std::move(debugRenderer).value();
    m_debugOverlay = m_config.settings.get<bool>("render.debug_draw", false);
    const auto size = m_window->pixelSize();
    auto target = render::SceneTarget::create(*m_device, size.width, size.height);
    if (!target)
    {
        return Error{"cannot create scene target: " + target.error().message};
    }
    m_sceneTarget = std::move(target).value();
    m_postSettings.tonemapper = render::tonemapperFromName(
        m_config.settings.get<std::string>("render.tonemap", "aces"), render::Tonemapper::Aces);
    m_postSettings.exposure = static_cast<f32>(m_config.settings.get<f64>("render.exposure", 1.0));
    initEnvironment();
    return {};
}

Result<void> Engine::initViewMesh()
{
    auto data = asset::loadGltf(m_config.viewMesh);
    if (!data)
    {
        return Error{"cannot load mesh: " + data.error().message};
    }
    auto mesh = render::Mesh::create(*m_device, data.value());
    if (!mesh)
    {
        return Error{"cannot upload mesh: " + mesh.error().message};
    }
    m_viewMesh = std::move(mesh).value();

    // Missing or broken textures are warnings (neutral fallbacks), not a reason to refuse the model.
    auto materials = render::MaterialSet::create(*m_device, data.value(), m_config.viewMesh.parent_path());
    if (!materials)
    {
        return Error{"cannot create materials: " + materials.error().message};
    }
    m_viewMaterials = std::move(materials).value();

    // Sun shadows (render.md): [render] shadow_* with the defaults 4 x 2048², 150 m.
    render::ShadowSettings shadows;
    shadows.cascades =
        static_cast<u32>(std::clamp<i64>(m_config.settings.get<i64>("render.shadow_cascades", 4), 1, 4));
    shadows.resolution = static_cast<u32>(
        std::clamp<i64>(m_config.settings.get<i64>("render.shadow_resolution", 2048), 256, 8192));
    shadows.distance = static_cast<f32>(m_config.settings.get<f64>("render.shadow_distance", 150.0));
    m_shadowDebug = m_config.settings.get<bool>("render.shadow_debug", false);
    auto shadowMap = render::ShadowMap::create(*m_device, shadows);
    if (!shadowMap)
    {
        return Error{"cannot create shadow map: " + shadowMap.error().message};
    }
    m_shadowMap = std::move(shadowMap).value();

    const auto anisotropy = static_cast<f32>(m_config.settings.get<f64>("render.anisotropy", 8.0));
    auto renderer = render::MeshRenderer::create(*m_device, *m_shaders, anisotropy, shadows);
    if (!renderer)
    {
        return Error{"cannot create mesh renderer: " + renderer.error().message};
    }
    m_meshRenderer = std::move(renderer).value();

    // Frame the model: look at its centre from the front-right, at 2.5x its radius.
    const AABB& bounds = m_viewMesh.bounds();
    const f32 radius = std::max(glm::length(bounds.extents()), 0.5f);

    if (m_config.ground)
    {
        // Ground plate under the model (it receives the shadows), 1 m texture tiles.
        const asset::MeshData plane =
            asset::makePlane(std::max(radius * 8.0f, 20.0f), 1.0f, Vec4(0.45f, 0.42f, 0.36f, 1.0f));
        auto ground = render::Mesh::create(*m_device, plane);
        auto groundMaterials = render::MaterialSet::create(*m_device, plane, {});
        if (!ground || !groundMaterials)
        {
            return Error{"cannot create ground plate"};
        }
        m_ground = std::move(ground).value();
        m_groundMaterials = std::move(groundMaterials).value();
    }

    // A warm "torch" above the front-right of the model shows the point lights.
    m_lights.clear();
    const Vec3 torchOffset = Vec3(0.6f, 0.8f, 0.6f) * radius;
    // The falloff is in metres (1 / (d² + 1)); scale the intensity so the torch lights any model size.
    const f32 torchIntensity = 2.0f * (glm::dot(torchOffset, torchOffset) + 1.0f);
    m_lights.add({bounds.center() + torchOffset, radius * 2.5f, Vec3(1.0f, 0.62f, 0.3f), torchIntensity});
    m_camera.transform.position = bounds.center() + glm::normalize(Vec3(0.6f, 0.4f, 1.0f)) * radius * 2.5f;
    m_camera.transform.rotation = lookRotation(bounds.center() - m_camera.transform.position);
    m_flyCamera.speed = std::max(radius, 1.0f);
    m_flyCamera.attach(m_camera);
    G7_LOG_INFO("engine", "viewing {} ({} vertices, {} submeshes, {} materials, {:.1f} m across)",
                fs::toUtf8(m_config.viewMesh), data.value().vertices.size(), m_viewMesh.submeshes().size(),
                m_viewMaterials.size(), radius * 2.0f);
    return {};
}

void Engine::initEnvironment()
{
    // Low warm evening sun, cool ambient and fog in the horizon colour of the dusk background, until
    // the sky and time of day (M4) drive these.
    m_environment.sunDirection = Vec3(0.6f, 0.25f, 0.4f);
    m_environment.sunColor = Vec3(1.0f, 0.72f, 0.5f);
    m_environment.sunIntensity = m_config.sun ? 1.6f : 0.0f;
    m_environment.ambientSky = Vec3(0.16f, 0.18f, 0.26f);
    m_environment.ambientGround = Vec3(0.07f, 0.06f, 0.05f);
    m_environment.fogColor = Vec3(0.0844f, 0.0395f, 0.0331f); // sRGB (0.32, 0.22, 0.20), the dusk horizon
    m_environment.fogStart = static_cast<f32>(m_config.settings.get<f64>("render.fog_start", 30.0));
    // Default density: 90 % fog at 300 m.
    m_environment.fogDensity = static_cast<f32>(
        m_config.settings.get<f64>("render.fog_density", render::fogDensityFor(0.9f, 300.0f, 30.0f)));
}

void Engine::renderScene(u32 width, u32 height)
{
    m_device->beginFrame(width, height, kClearColor); // stats, window cleared
    if (auto resized = m_sceneTarget.resize(*m_device, width, height); !resized)
    {
        G7_LOG_ERROR("engine", "{}", resized.error().message);
        return;
    }
    m_camera.aspect = height > 0 ? static_cast<f32>(width) / static_cast<f32>(height) : 1.0f;

    // Scene into the linear HDR target: background, then meshes (their shadow pass rebinds it).
    m_device->bindFramebuffer(&m_sceneTarget.framebuffer());
    m_device->setViewport(0, 0, width, height);
    m_device->clear(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    m_backgroundProgram->setUniform("uInverseViewProjection", glm::inverse(m_camera.viewProjection()));
    m_backgroundProgram->setUniform("uCameraPosition", m_camera.transform.position);
    m_backgroundProgram->setUniform("uHorizonColor", m_environment.fogColor);
    m_device->bindPipeline(m_backgroundPipeline);
    m_device->draw(3); // fullscreen triangle from gl_VertexID
    drawViewMesh(width, height);

    // Tonemap into the window.
    m_device->bindFramebuffer(nullptr);
    m_post.apply(*m_device, m_sceneTarget, width, height, m_postSettings);

    // Debug drawing on top, depth-tested against the scene.
    if (m_debugOverlay)
    {
        addDebugOverlay(width, height);
        m_debugRenderer.render(*m_device, m_debugDraw, m_camera, &m_sceneTarget.depth(), width, height);
    }
}

void Engine::drawViewMesh(u32 width, u32 height)
{
    if (m_viewMesh.submeshes().empty())
    {
        return;
    }
    // The ground plate sits at the model's lowest point.
    const Mat4 groundModel = glm::translate(Mat4(1.0f), Vec3(0.0f, m_viewMesh.bounds().min.y, 0.0f));

    // Shadow pass: the model into every cascade (the flat ground cannot shadow anything above it).
    render::ShadowFrame shadowFrame;
    if (m_environment.sunIntensity > 0.0f)
    {
        m_cascades = render::computeCascades(m_camera, m_environment.sunDirection, m_shadowMap.settings());
        m_shadowMap.begin(*m_device);
        for (u32 i = 0; i < m_cascades.size(); ++i)
        {
            m_shadowMap.beginCascade(*m_device, i);
            m_meshRenderer.drawShadow(*m_device, m_viewMesh, m_viewMaterials, Mat4(1.0f), m_cascades[i]);
        }
        shadowFrame = {&m_shadowMap, m_cascades, &m_camera, m_shadowDebug};
        m_device->bindFramebuffer(&m_sceneTarget.framebuffer());
        m_device->setViewport(0, 0, width, height);
    }

    m_meshRenderer.setLighting(*m_device, m_environment, m_lights, shadowFrame.map ? &shadowFrame : nullptr);
    if (!m_ground.submeshes().empty())
    {
        m_meshRenderer.draw(*m_device, m_ground, m_groundMaterials, groundModel, m_camera);
    }
    m_meshRenderer.draw(*m_device, m_viewMesh, m_viewMaterials, Mat4(1.0f), m_camera);
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

void Engine::setDebugOverlay(bool enabled) noexcept
{
    if (enabled != m_debugOverlay)
    {
        m_debugOverlay = enabled;
        G7_LOG_INFO("engine", "debug overlay {}", enabled ? "on" : "off");
    }
}

void Engine::addDebugOverlay(u32 width, u32 height)
{
    // Frame statistics in the top-left corner (until the ImGui overlay exists).
    const render::FrameStats& stats = m_device->stats();
    const f64 ms = m_smoothedFrameSeconds * 1000.0;
    const Vec3& p = m_camera.transform.position;
    m_debugDraw.screenText(
        Vec2(8.0f, 8.0f),
        std::format("{:.0f} fps  {:.2f} ms{}\n{} draws  {:.1f}k tris\n{}x{}  cam {:.1f} {:.1f} {:.1f}",
                    ms > 0.0 ? 1000.0 / ms : 0.0, ms, m_paused ? "  PAUSED" : "", stats.drawCalls,
                    stats.triangles / 1000.0, width, height, p.x, p.y, p.z),
        Vec4(1.0f), 2.0f);

    // World origin and the --view-mesh scene: ground grid, bounds with the file name, torches.
    m_debugDraw.axes(Mat4(1.0f), 1.0f);
    if (m_viewMesh.submeshes().empty())
    {
        return;
    }
    const AABB& bounds = m_viewMesh.bounds();
    const f32 size = std::max(glm::length(bounds.max - bounds.min), 1.0f);
    const f32 spacing = std::exp2(std::round(std::log2(size / 8.0f))); // ~8 cells across the model
    m_debugDraw.grid(Vec3(0.0f, bounds.min.y, 0.0f), spacing * 40.0f, spacing,
                     {Vec4(0.6f, 0.6f, 0.6f, 0.35f)});
    m_debugDraw.box(bounds, {Vec4(1.0f, 0.85f, 0.2f, 1.0f)});
    m_debugDraw.text(Vec3(bounds.center().x, bounds.max.y, bounds.center().z) +
                         Vec3(0.0f, size * 0.08f, 0.0f),
                     fs::toUtf8(m_config.viewMesh.filename()), {Vec4(1.0f, 0.85f, 0.2f, 1.0f)}, 2.0f);
    for (const render::PointLight& light : m_lights.lights())
    {
        const render::DebugStyle style{Vec4(1.0f, 0.55f, 0.2f, 0.8f)};
        m_debugDraw.cross(light.position, size * 0.05f, style);
        m_debugDraw.sphere(light.position, light.radius, style);
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
    m_debugRenderer = {};
    m_debugDraw.clear();
    m_meshRenderer = {};
    m_post = {};
    m_sceneTarget = {};
    m_shadowMap = {};
    m_groundMaterials = {};
    m_ground = {};
    m_viewMaterials = {};
    m_viewMesh = {};
    m_backgroundPipeline = {};
    m_backgroundProgram = nullptr;
    m_shaders.reset();
    m_device.reset(); // GL objects need the context
    m_glContext.reset();
    m_window.reset();
    m_initialized = false;
}
} // namespace g7

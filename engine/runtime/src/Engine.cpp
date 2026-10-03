#include <g7/ai/Ai.hpp>
#include <g7/animation/Animation.hpp>
#include <g7/asset/Asset.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/Procedural.hpp>
#include <g7/audio/Audio.hpp>
#include <g7/core/Clock.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/core/Version.hpp>
#include <g7/gameplay/Gameplay.hpp>
#include <g7/physics/Physics.hpp>
#include <g7/platform/Platform.hpp>
#include <g7/platform/Time.hpp>
#include <g7/render/Render.hpp>
#include <g7/runtime/AssetMounts.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/save/Save.hpp>
#include <g7/script/Script.hpp>
#include <g7/ui/Ui.hpp>
#include <g7/world/StartPoints.hpp>
#include <g7/world/World.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <format>
#include <numeric>
#include <string_view>
#include <utility>

namespace g7
{
namespace
{
/// Dusk colour until there is a sky (M4).
const Vec4 kClearColor{0.10f, 0.11f, 0.14f, 1.0f};
/// Hot reload of shaders and assets: on in development (debug) builds, off in release builds.
#ifdef NDEBUG
constexpr bool kHotReloadDefault = false;
#else
constexpr bool kHotReloadDefault = true;
#endif
/// Folders of files given on the command line (outside the mounts) overlay everything.
constexpr i32 kLocalMountPriority = 1000;
/// Evening sun of the interim environment (until the time of day, M4).
constexpr f32 kSunIntensity = 1.6f;

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
    // Game time: [time] minute_seconds (real seconds per game minute), start "HH:MM" (or --time).
    m_gameTime.setSecondsPerMinute(m_config.settings.get<f64>("time.minute_seconds", 4.0));
    const std::string startTime = m_config.startTime.empty()
                                      ? m_config.settings.get<std::string>("time.start", "08:00")
                                      : m_config.startTime;
    u32 hour = 24;
    u32 minute = 60;
    const char* const end = startTime.data() + startTime.size();
    const auto parsedHour = std::from_chars(startTime.data(), end, hour);
    if (parsedHour.ec == std::errc{} && parsedHour.ptr != end && *parsedHour.ptr == ':')
    {
        const auto parsedMinute = std::from_chars(parsedHour.ptr + 1, end, minute);
        minute = parsedMinute.ec == std::errc{} && parsedMinute.ptr == end ? minute : 60;
    }
    if (hour > 23 || minute > 59)
    {
        return Error{"invalid start time '" + startTime + "' (expected HH:MM)"};
    }
    m_gameTime.setTime(0, hour, minute);

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

    // Camera from [camera] (defaults: 70° vertical FOV, 0.1–1500 m, 0.1°/px mouse). Before the scene,
    // which places the camera.
    m_camera.fovY = toRadians(static_cast<f32>(m_config.settings.get<f64>("camera.fov", 70.0)));
    m_camera.nearPlane = static_cast<f32>(m_config.settings.get<f64>("camera.near", 0.1));
    m_camera.farPlane = static_cast<f32>(m_config.settings.get<f64>("camera.far", 1500.0));
    m_camera.transform.position = Vec3(0.0f, 1.8f, 6.0f);
    m_flyCamera.sensitivity =
        toRadians(static_cast<f32>(m_config.settings.get<f64>("camera.mouse_sensitivity", 0.1)));
    m_flyCamera.speed = static_cast<f32>(m_config.settings.get<f64>("camera.fly_speed", 10.0));
    m_flyCamera.attach(m_camera);

    if (auto assets = initAssets(); !assets)
    {
        return assets;
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
            m_geometry = std::make_unique<render::GeometryArena>();
            m_glContext->setVSync(m_config.window.vsync);

            if (auto result = initShaders(); !result)
            {
                return result;
            }
            if (!m_config.world.empty() || !m_config.scene.empty() || !m_config.viewMesh.empty())
            {
                auto result = initSceneRendering();
                if (result)
                {
                    result = !m_config.world.empty()   ? initWorld()
                             : !m_config.scene.empty() ? initScene()
                                                       : initViewMesh();
                }
                if (result && !m_config.saveWorld.empty())
                {
                    result = saveWorld(m_config.saveWorld);
                }
                if (!result)
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
        if (m_actions.pressed(m_input, platform::Action::DebugUi))
        {
            setDebugUiVisible(!m_debugUiVisible);
        }
        // The debug UI sees the input first: what it uses (hovered window, focused text field) the
        // game ignores.
        runDebugUi(realSeconds);
        const bool uiMouse = m_debugUiFrame && m_debugUi.wantsMouse();
        const bool uiKeyboard = m_debugUiFrame && m_debugUi.wantsKeyboard();
        if (m_config.benchmark)
        {
            updateBenchmark(realSeconds); // drives the camera itself
        }
        else
        {
            updateDebugCamera(realSeconds, !uiMouse, !uiKeyboard);
        }
        for (EngineTool* tool : m_tools)
        {
            tool->update(*this, realSeconds, uiMouse, uiKeyboard);
        }

        if (!uiKeyboard)
        {
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
        // Triggers notice the camera until the player exists (M5).
        const world::TriggerProbe camera{kCameraProbe, m_camera.transform.position, true};
        m_triggers.update(m_scene, std::span(&camera, 1));
        m_gameTime.advance(m_fixedStep.step()); // global: keeps running across level changes
        ++m_simTicks;
        if (m_pendingWorldChange)
        {
            break; // the level change happens before the next step: the old world is done
        }
    }
    if (m_pendingWorldChange)
    {
        performWorldChange(); // between simulation and rendering, never inside either
    }
    // Finished asset loads become visible here, once per frame on the main thread; hot reload
    // looks for changed files first and re-uploads affected models afterwards.
    m_assets->checkForChanges(platform::nowSeconds());
    m_assets->update();
    if (m_device)
    {
        refreshReloadedModels();
    }

    {
        G7_PROFILE_SCOPE("Engine::render");
        if (m_device)
        {
            // TODO(M2): scene rendering with frameAlpha(); for now the frame is only cleared.
            const auto size = m_window->pixelSize();
            m_shaders->update(platform::nowSeconds());
            renderScene(size.width, size.height);
            const bool lastFrame =
                m_quitRequested || (m_config.maxFrames != 0 && m_frameCount + 1 >= m_config.maxFrames);
            if (lastFrame && !m_config.screenshot.empty())
            {
                saveScreenshot(size.width, size.height);
            }
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

    auto debugUi = ui::DebugUi::create(m_device.get(), m_shaders.get(), m_window->displayScale());
    if (!debugUi)
    {
        return Error{"cannot create debug UI: " + debugUi.error().message};
    }
    m_debugUi = std::move(debugUi).value();
    m_debugUiVisible = m_config.settings.get<bool>("render.debug_ui", false);
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

Result<void> Engine::initSceneRendering()
{
    // Sun shadows (render.md): [render] shadow_* with the defaults 4 x 2048², 150 m.
    render::ShadowSettings shadows;
    shadows.cascades =
        static_cast<u32>(std::clamp<i64>(m_config.settings.get<i64>("render.shadow_cascades", 4), 1, 4));
    shadows.resolution = static_cast<u32>(
        std::clamp<i64>(m_config.settings.get<i64>("render.shadow_resolution", 2048), 256, 8192));
    shadows.distance = static_cast<f32>(m_config.settings.get<f64>("render.shadow_distance", 150.0));
    m_shadowDebug = m_config.settings.get<bool>("render.shadow_debug", false);
    m_cullSettings.viewDistance = static_cast<f32>(m_config.settings.get<f64>("render.view_distance", 400.0));
    m_cullSettings.sizeCull = static_cast<f32>(m_config.settings.get<f64>("render.size_cull", 0.005));
    // multi_draw: "auto" (default) batches except on Intel GPUs - there the GPU is the limit and the
    // batches measured ~10 % slower (render.md); true/false or "on"/"off" force it.
    const auto forced = m_config.settings.find<bool>("render.multi_draw");
    const std::string multiMode =
        forced ? (*forced ? "on" : "off") : m_config.settings.get<std::string>("render.multi_draw", "auto");
    const bool intel = toLower(m_device->info().vendor).find("intel") != std::string::npos;
    m_multiDraw = multiMode == "on" || (multiMode == "auto" && !intel);
    G7_LOG_INFO("engine", "multi-draw batches {} ({})", m_multiDraw ? "on" : "off",
                multiMode != "auto" ? "forced"
                : intel             ? "auto: off on Intel GPUs"
                                    : "auto");
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
    return {};
}

Result<void> Engine::initAssets()
{
    // Mounts from [assets]; development builds also see the repository's assets/ folder.
#if defined(G7_DEV_ASSET_ROOT)
    const fs::Path devRoot = fs::fromUtf8(G7_DEV_ASSET_ROOT);
#else
    const fs::Path devRoot;
#endif
    auto mounts = assetMounts(m_config.settings, fs::baseDirectories().gameDir, devRoot);
    if (!mounts)
    {
        return Error{"invalid [assets] configuration: " + mounts.error().message};
    }
    for (const MountSpec& spec : mounts.value())
    {
        if (auto id = m_vfs.mount(spec.source, spec.priority, spec.mountPoint); !id)
        {
            // A missing data folder must not stop the engine (e.g. no cooked data yet).
            G7_LOG_WARN("engine", "asset mount skipped: {}", id.error().message);
        }
    }
    asset::AssetManagerDesc desc;
    desc.hotReload = m_config.settings.get<bool>("assets.hot_reload", kHotReloadDefault);
    m_assets = std::make_unique<asset::AssetManager>(m_vfs, desc);
    if (desc.hotReload)
    {
        G7_LOG_INFO("engine", "asset hot reload on");
    }
    return {};
}

Result<std::string> Engine::resolveAssetArgument(const fs::Path& argument)
{
    const std::string text = fs::toUtf8(argument);
    if (auto path = asset::normalizeVfsPath(text); path && m_vfs.exists(path.value()))
    {
        return path.value();
    }
    // A file on disk outside the mounts (e.g. a downloaded model): mount its folder under local/,
    // so files next to it (glTF buffers, textures, a scene's models) resolve too.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(argument, ec))
    {
        return Error{"'" + text + "' is neither a file in the VFS nor on disk"};
    }
    const fs::Path folder = std::filesystem::absolute(argument, ec).parent_path();
    if (auto id = m_vfs.mount(folder, kLocalMountPriority, "local"); !id)
    {
        return id.error();
    }
    return "local/" + fs::toUtf8(argument.filename());
}

const LoadedModel* Engine::model(std::string_view path) const
{
    const auto found = m_models.find(path);
    return found != m_models.end() ? found->second.get() : nullptr;
}

void Engine::requestImages(LoadedModel& loaded, std::vector<std::string>* imagePaths)
{
    // External images from the VFS root (cooked meshes) or next to the mesh (glTF).
    const asset::MeshData& data = *loaded.source.get();
    loaded.images.assign(data.images.size(), {});
    for (usize i = 0; i < data.images.size(); ++i)
    {
        const asset::ImageSource& source = data.images[i];
        if (source.uri.empty())
        {
            continue; // embedded: MaterialSet decodes it
        }
        const auto candidates = imageCandidates(loaded.name, source.uri);
        const auto found = std::find_if(candidates.begin(), candidates.end(),
                                        [&](const std::string& c) { return m_vfs.exists(c); });
        if (found == candidates.end())
        {
            G7_LOG_WARN("engine", "{}: image '{}' not found in the VFS", loaded.name, source.uri);
            continue;
        }
        loaded.images[i] = m_assets->load<asset::TextureData>(*found);
        if (imagePaths != nullptr &&
            std::none_of(imagePaths->begin(), imagePaths->end(),
                         [&](const std::string& p) { return equalsIgnoreCase(p, *found); }))
        {
            imagePaths->push_back(*found);
        }
    }
}

Result<void> Engine::uploadModel(LoadedModel& loaded)
{
    // Built aside and swapped in only on success: a broken reload keeps the working model.
    const asset::MeshData& data = *loaded.source.get();
    auto mesh = render::Mesh::create(*m_device, *m_geometry, data);
    if (!mesh)
    {
        return Error{"cannot upload mesh " + loaded.name + ": " + mesh.error().message};
    }
    // Missing or broken textures are warnings (neutral fallbacks), not a reason to refuse the model.
    auto materials = render::MaterialSet::create(
        *m_device, data,
        [&](const asset::ImageSource& source) -> const asset::TextureData*
        {
            const auto index = static_cast<usize>(&source - data.images.data());
            const asset::Handle<asset::TextureData>& image = loaded.images[index];
            if (image.failed() && image.valid())
            {
                G7_LOG_WARN("engine", "{}: {}", loaded.name, image.error());
            }
            return image.get();
        },
        m_meshRenderer.defaults()); // shared neutral textures: models batch together
    if (!materials)
    {
        return Error{"cannot create materials for " + loaded.name + ": " + materials.error().message};
    }
    loaded.mesh = std::move(mesh).value();
    loaded.materials = std::move(materials).value();
    loaded.sourceVersion = loaded.source.version();
    loaded.imageVersions.resize(loaded.images.size());
    for (usize i = 0; i < loaded.images.size(); ++i)
    {
        loaded.imageVersions[i] = loaded.images[i].version();
    }
    G7_LOG_DEBUG("engine", "uploaded {} ({} vertices, {} submeshes, {} materials)", loaded.name,
                 data.vertices.size(), loaded.mesh.submeshes().size(), loaded.materials.size());
    return {};
}

Result<void> Engine::loadModels(const std::vector<std::string>& paths)
{
    // 1. Meshes on the asset workers, in parallel.
    const Stopwatch timer;
    std::vector<std::unique_ptr<LoadedModel>> pending;
    for (const std::string& path : paths)
    {
        const bool queued = std::any_of(pending.begin(), pending.end(),
                                        [&](const auto& m) { return equalsIgnoreCase(m->name, path); });
        if (!queued && model(path) == nullptr)
        {
            auto loaded = std::make_unique<LoadedModel>();
            loaded->name = path;
            // The scene names the model; a cooked .g7mesh of it is loaded instead when present.
            loaded->source = m_assets->load<asset::MeshData>(preferCooked(m_vfs, path));
            pending.push_back(std::move(loaded));
        }
    }
    m_assets->waitAll();

    // 2. Their images (repeated paths are shared by the cache).
    std::vector<std::string> imagePaths; // distinct, for the log
    for (const auto& loaded : pending)
    {
        if (loaded->source.failed())
        {
            return Error{"cannot load mesh: " + loaded->source.error()};
        }
        requestImages(*loaded, &imagePaths);
    }
    m_assets->waitAll();

    // 3. Upload on the main thread.
    for (auto& loaded : pending)
    {
        if (auto uploaded = uploadModel(*loaded); !uploaded)
        {
            return uploaded;
        }
        std::string key = loaded->name;
        m_models.emplace(std::move(key), std::move(loaded));
    }
    if (!pending.empty())
    {
        G7_LOG_INFO("engine", "loaded {} models and {} images in {:.0f} ms", pending.size(),
                    imagePaths.size(), timer.elapsedSeconds() * 1000.0);
    }
    return {};
}

void Engine::refreshReloadedModels()
{
    // Hot reload: a model whose mesh or one of its images has a new version is uploaded again; all
    // its instances use it right away.
    for (auto& [path, loaded] : m_models)
    {
        const bool meshChanged =
            loaded->source.isReady() && loaded->source.version() != loaded->sourceVersion;
        bool imagesChanged = false;
        for (usize i = 0; i < loaded->images.size() && i < loaded->imageVersions.size(); ++i)
        {
            imagesChanged = imagesChanged || (loaded->images[i].isReady() &&
                                              loaded->images[i].version() != loaded->imageVersions[i]);
        }
        if (!meshChanged && !imagesChanged)
        {
            continue;
        }
        if (meshChanged)
        {
            // The new mesh may reference other images.
            requestImages(*loaded, nullptr);
            m_assets->waitAll();
        }
        if (auto uploaded = uploadModel(*loaded); !uploaded)
        {
            G7_LOG_WARN("engine", "hot reload of {} failed, keeping the previous version: {}", path,
                        uploaded.error().message);
            loaded->sourceVersion = loaded->source.version(); // do not retry every frame
            continue;
        }
        for (SceneInstance& instance : m_instances)
        {
            if (instance.model == loaded.get())
            {
                instance.bounds = loaded->mesh.bounds().transformed(instance.transform);
                m_cullGridDirty = true;
            }
        }
        G7_LOG_INFO("engine", "hot reload: {} updated", path);
    }
}

void Engine::addInstance(const LoadedModel& model, const Mat4& transform, bool sizeCullable, world::VobId vob)
{
    const AABB bounds = model.mesh.bounds().transformed(transform);
    // Scene bounds without the ground plate (debug grid, overlay).
    if (&model != m_groundModel.get())
    {
        m_sceneBounds =
            m_sceneBounds.max.x < m_sceneBounds.min.x
                ? bounds
                : AABB{glm::min(m_sceneBounds.min, bounds.min), glm::max(m_sceneBounds.max, bounds.max)};
    }
    m_instances.push_back({&model, transform, bounds, sizeCullable, vob});
    m_cullGridDirty = true;
}

Result<void> Engine::addGround(f32 size, const Vec3& color, f32 height)
{
    // Ground plate (it receives the shadows), 1 m texture tiles.
    const asset::MeshData plane = asset::makePlane(size, 1.0f, Vec4(color, 1.0f));
    auto mesh = render::Mesh::create(*m_device, *m_geometry, plane);
    auto materials = render::MaterialSet::create(*m_device, plane, render::MaterialSet::ImageLookup{},
                                                 m_meshRenderer.defaults());
    if (!mesh || !materials)
    {
        return Error{"cannot create ground plate"};
    }
    m_groundModel = std::make_unique<LoadedModel>();
    m_groundModel->mesh = std::move(mesh).value();
    m_groundModel->materials = std::move(materials).value();
    m_groundModel->name = "ground";
    addInstance(*m_groundModel, glm::translate(Mat4(1.0f), Vec3(0.0f, height, 0.0f)), false);
    return {};
}

void Engine::setViewpoint(const SceneViewpoint& viewpoint)
{
    m_camera.transform.position = viewpoint.position;
    m_camera.transform.rotation = quatFromEuler(viewpoint.pitch, viewpoint.yaw, 0.0f);
    m_flyCamera.attach(m_camera);
}

Result<void> Engine::initViewMesh()
{
    auto path = resolveAssetArgument(m_config.viewMesh);
    if (!path)
    {
        return Error{"cannot load mesh: " + path.error().message};
    }
    if (auto loadedModels = loadModels({path.value()}); !loadedModels)
    {
        return loadedModels;
    }
    const LoadedModel& loaded = *model(path.value());
    addInstance(loaded, Mat4(1.0f));
    m_sceneName = fs::toUtf8(fs::fromUtf8(loaded.name).filename());

    // Frame the model: look at its centre from the front-right, at 2.5x its radius.
    const AABB bounds = loaded.mesh.bounds();
    const f32 radius = std::max(glm::length(bounds.extents()), 0.5f);
    if (m_config.ground)
    {
        if (auto ground = addGround(std::max(radius * 8.0f, 20.0f), Vec3(0.45f, 0.42f, 0.36f), bounds.min.y);
            !ground)
        {
            return ground;
        }
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
    G7_LOG_INFO("engine", "viewing {} ({} submeshes, {} materials, {:.1f} m across)", loaded.name,
                loaded.mesh.submeshes().size(), loaded.materials.size(), radius * 2.0f);
    return {};
}

Result<void> Engine::initScene()
{
    auto scenePath = resolveAssetArgument(m_config.scene);
    auto scene = scenePath ? loadSceneFile(m_vfs, scenePath.value()) : Result<SceneFile>(scenePath.error());
    if (!scene)
    {
        return Error{"cannot load scene: " + scene.error().message};
    }
    const SceneFile& file = scene.value();
    // The test scene becomes world vobs, so it renders (and saves, --save-world) like a .g7world.
    for (const SceneObject& object : file.objects)
    {
        auto vob = m_scene.spawnVob(
            {fs::toUtf8(fs::fromUtf8(object.mesh).stem()), Transform::fromMatrix(object.transform)});
        if (!vob)
        {
            return vob.error();
        }
        m_scene.set<world::MeshRef>(vob.value(), {object.mesh});
    }
    for (const SceneLight& light : file.lights)
    {
        Transform at;
        at.position = light.position;
        auto vob = m_scene.spawnVob({"LIGHT", at});
        if (!vob)
        {
            return vob.error();
        }
        m_scene.set<world::LightSource>(vob.value(), {light.color, light.radius, light.intensity, 0.0f});
    }
    if (auto instantiated = instantiateScene(); !instantiated)
    {
        return instantiated;
    }
    if (file.groundSize > 0.0f && m_config.ground)
    {
        if (auto ground = addGround(file.groundSize, file.groundColor, 0.0f); !ground)
        {
            return ground;
        }
    }

    // Overrides of the interim environment (set up by initEnvironment before).
    const SceneEnvironment& env = file.environment;
    m_environment.sunDirection = env.sunDirection.value_or(m_environment.sunDirection);
    m_environment.sunColor = env.sunColor.value_or(m_environment.sunColor);
    if (m_config.sun)
    {
        m_environment.sunIntensity = env.sunIntensity.value_or(m_environment.sunIntensity);
    }
    m_environment.ambientSky = env.ambientSky.value_or(m_environment.ambientSky);
    m_environment.ambientGround = env.ambientGround.value_or(m_environment.ambientGround);
    m_environment.fogColor = env.fogColor.value_or(m_environment.fogColor);
    m_environment.fogStart = env.fogStart.value_or(m_environment.fogStart);
    m_environment.fogDensity = env.fogDensity.value_or(m_environment.fogDensity);

    m_viewpoints = file.viewpoints;
    if (!m_viewpoints.empty())
    {
        if (m_config.viewpoint >= m_viewpoints.size())
        {
            G7_LOG_WARN("engine", "scene has no viewpoint {}, using 0", m_config.viewpoint);
        }
        setViewpoint(m_viewpoints[m_config.viewpoint < m_viewpoints.size() ? m_config.viewpoint : 0]);
    }
    m_sceneName = fs::toUtf8(fs::fromUtf8(scenePath.value()).filename());
    G7_LOG_INFO("engine", "scene {}: {} objects ({} models), {} lights, {} viewpoints", scenePath.value(),
                m_instances.size(), m_models.size(), file.lights.size(), m_viewpoints.size());
    return {};
}

Result<void> Engine::instantiateScene()
{
    // Models of all mesh vobs in one batch, then one render instance per vob at its world matrix.
    m_scene.updateTransforms();
    std::vector<std::string> meshes;
    m_scene.each<world::MeshRef>([&](entt::entity, const world::MeshRef& mesh)
                                 { meshes.push_back(mesh.path); });
    if (auto loaded = loadModels(meshes); !loaded)
    {
        return loaded;
    }
    m_scene.each<world::Vob, world::MeshRef, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::MeshRef& mesh,
            const world::WorldTransform& world)
        {
            // Cached under the path of its first use; the VFS matches case-insensitively.
            const LoadedModel* loaded = model(mesh.path);
            if (loaded == nullptr)
            {
                const auto found = std::find_if(m_models.begin(), m_models.end(), [&](const auto& entry)
                                                { return equalsIgnoreCase(entry.first, mesh.path); });
                loaded = found->second.get();
            }
            addInstance(*loaded, world.matrix, mesh.category == world::VobCategory::Deco, vob.id);
        });
    m_lights.clear();
    m_scene.each<world::LightSource, world::WorldTransform>(
        [&](entt::entity, const world::LightSource& light, const world::WorldTransform& world)
        { m_lights.add({Vec3(world.matrix[3]), light.range, light.color, light.intensity}); });
    return {};
}

Result<void> Engine::refreshScene()
{
    // Everything but the ground plate is rebuilt from the scene; models stay cached, new ones load.
    std::erase_if(m_instances,
                  [&](const SceneInstance& instance) { return instance.model != m_groundModel.get(); });
    m_sceneBounds = AABB{Vec3(1.0f), Vec3(-1.0f)};
    m_cullGridDirty = true;
    return instantiateScene();
}

std::optional<fs::Path> Engine::worldSourceFile() const
{
    return m_worldPath.empty() ? std::nullopt : m_vfs.diskPath(m_worldPath);
}

void Engine::addTool(EngineTool& tool)
{
    m_tools.push_back(&tool);
}

Result<void> Engine::initWorld()
{
    auto path = resolveAssetArgument(m_config.world);
    auto file = path ? world::loadWorldFile(m_vfs, path.value()) : Result<world::WorldFile>(path.error());
    if (!file)
    {
        return Error{"cannot load world: " + file.error().message};
    }
    return loadWorld(path.value(), std::move(file).value(), m_config.start);
}

Result<void> Engine::loadWorld(const std::string& path, world::WorldFile file, std::string_view start)
{
    const Stopwatch timer;
    if (auto spawned = world::spawnWorld(m_scene, file); !spawned)
    {
        return Error{"cannot load world: " + spawned.error().message};
    }
    if (const auto& ref = file.terrain)
    {
        auto heightfield = world::Heightfield::load(m_vfs, *ref);
        if (!heightfield)
        {
            return Error{"cannot load world: " + heightfield.error().message};
        }
        m_heightfield = std::move(heightfield).value();
        auto terrain = render::TerrainRenderer::create(*m_device, *m_shaders, m_heightfield.renderDesc(),
                                                       m_shadowMap.settings());
        if (!terrain)
        {
            return Error{"cannot create terrain: " + terrain.error().message};
        }
        m_terrain = std::move(terrain).value();
        m_hasTerrain = true;
        G7_LOG_INFO("engine", "terrain {} ({} x {} samples, {} m cells, {} chunks)", ref->heightmap,
                    ref->width, ref->height, ref->cellSize, m_terrain.chunkCount());
        loadTerrainSurface(*ref);
    }
    if (auto instantiated = instantiateScene(); !instantiated)
    {
        return instantiated;
    }
    // Without terrain a ground plate below the world; a camera overlooking the vobs (or the terrain
    // of a world without vobs).
    const bool hasVobs = m_sceneBounds.max.x >= m_sceneBounds.min.x;
    if (!hasVobs && m_hasTerrain)
    {
        const AABB area = m_heightfield.bounds();
        const Vec3 centre = area.center();
        m_camera.transform.position =
            Vec3(centre.x, m_heightfield.heightAt(centre.x, centre.z) + 60.0f, centre.z + 150.0f);
        m_camera.transform.rotation =
            lookRotation(Vec3(centre.x, m_heightfield.heightAt(centre.x, centre.z), centre.z) -
                         m_camera.transform.position);
        m_flyCamera.speed = 30.0f;
        m_flyCamera.attach(m_camera);
    }
    if (hasVobs)
    {
        const Vec3 size = m_sceneBounds.max - m_sceneBounds.min;
        const f32 radius = std::max(glm::length(size) * 0.5f, 1.0f);
        if (m_config.ground && !m_hasTerrain)
        {
            if (auto ground = addGround(std::max(radius * 8.0f, 100.0f), Vec3(0.34f, 0.31f, 0.24f),
                                        m_sceneBounds.min.y);
                !ground)
            {
                return ground;
            }
        }
        const Vec3 centre = m_sceneBounds.center();
        m_camera.transform.position = centre + glm::normalize(Vec3(0.6f, 0.5f, 1.0f)) * radius * 1.2f;
        m_camera.transform.rotation = lookRotation(centre - m_camera.transform.position);
        m_flyCamera.speed = std::clamp(radius * 0.2f, 5.0f, 50.0f);
        m_flyCamera.attach(m_camera);
    }
    if (auto started = applyStartPoint(start); !started)
    {
        return Error{"cannot load world: " + started.error().message};
    }
    m_triggers.reset();
    m_triggers.setCallback(
        [this](const world::TriggerEvent& event)
        {
            // Until scripts exist (M7) the events are logged; level changes act right away.
            const entt::entity e = m_scene.findById(event.trigger);
            G7_LOG_INFO("engine", "trigger {} {}{}{}",
                        event.kind == world::TriggerEvent::Kind::Enter ? "enter" : "leave",
                        e != entt::null ? m_scene.get<world::Vob>(e)->nameText
                                        : std::to_string(event.trigger.value),
                        event.function.empty() ? "" : " -> ", event.function);
            const world::TriggerVolume* volume =
                e != entt::null ? m_scene.get<world::TriggerVolume>(e) : nullptr;
            if (event.kind == world::TriggerEvent::Kind::Enter && volume != nullptr &&
                !volume->changeWorld.empty())
            {
                requestWorldChange(volume->changeWorld, volume->changeStart);
            }
        });
    // Arrival: triggers the camera already stands in fire only after it left them (no bouncing back
    // through a level change next to the start point).
    m_scene.updateTransforms();
    const world::TriggerProbe camera{kCameraProbe, m_camera.transform.position, true};
    m_triggers.prime(m_scene, std::span(&camera, 1));
    m_scene.each<world::Vob, world::TriggerVolume>(
        [&](entt::entity, const world::Vob& vob, const world::TriggerVolume& volume)
        {
            if (!volume.changeWorld.empty() && m_triggers.isInside(vob.id, kCameraProbe))
            {
                G7_LOG_WARN("engine",
                            "{}: the start lies inside the level change {} (fires only after leaving it)",
                            path, vob.nameText);
            }
        });
    m_worldPath = path;
    m_worldFile = std::move(file);
    m_worldFile.vobs.clear(); // the scene holds them; captured again when the world is left
    m_sceneName = m_worldFile.name.empty() ? path : m_worldFile.name;
    G7_LOG_INFO("engine", "world {}: {} vobs, {} rendered, {} models, {} lights ({:.0f} ms)", path,
                m_scene.vobCount(), m_instances.size(), m_models.size(), m_lights.lights().size(),
                timer.elapsedSeconds() * 1000.0);
    return {};
}

void Engine::requestWorldChange(std::string world, std::string start)
{
    m_pendingWorldChange = PendingWorldChange{std::move(world), std::move(start)};
}

void Engine::unloadWorld()
{
    m_scene.clear();
    m_instances.clear();
    m_cullGridDirty = true;
    m_lights.clear();
    m_terrain = {};
    m_heightfield = {};
    m_hasTerrain = false;
    m_groundModel.reset();
    m_sceneBounds = AABB{Vec3(1.0f), Vec3(-1.0f)}; // empty
    m_triggers.reset();
}

void Engine::performWorldChange()
{
    const PendingWorldChange change = std::move(*m_pendingWorldChange);
    m_pendingWorldChange.reset();
    const std::string key = toLower(change.world);
    // The target: as left earlier (its state kept) or from its file.
    world::WorldFile target;
    std::set<u64> spent;
    if (const auto left = m_leftWorlds.find(key); left != m_leftWorlds.end())
    {
        target = left->second.file;
        spent = left->second.spentTriggers;
    }
    else
    {
        auto file = world::loadWorldFile(m_vfs, change.world);
        if (!file)
        {
            G7_LOG_WARN("engine", "level change to {} failed, staying here: {}", change.world,
                        file.error().message);
            return;
        }
        target = std::move(file).value();
    }
    const bool hasStart = std::any_of(
        target.vobs.begin(), target.vobs.end(), [&](const world::WorldFileVob& vob)
        { return vob.type == world::VobType::Start && equalsIgnoreCase(vob.name, change.start); });
    if (!hasStart)
    {
        G7_LOG_WARN("engine", "level change to {} failed, staying here: no start point '{}'", change.world,
                    change.start);
        return;
    }

    // Keep the world we leave as it is now (moved or removed vobs, spent once-triggers).
    const std::string leftPath = m_worldPath;
    world::WorldFile leaving = m_worldFile;
    const world::WorldFile captured = world::captureWorld(m_scene, m_worldFile.name);
    leaving.vobs = captured.vobs;
    leaving.nextVobId = captured.nextVobId;
    m_leftWorlds[toLower(leftPath)] = LeftWorld{leaving, m_triggers.spentTriggers()};

    G7_LOG_INFO("engine", "level change: {} -> {} ({})", leftPath, change.world, change.start);
    unloadWorld();
    if (auto loaded = loadWorld(change.world, std::move(target), change.start); !loaded)
    {
        // Not expected (the target was read and checked); go back to where we were.
        G7_LOG_ERROR("engine", "{}; returning to {}", loaded.error().message, leftPath);
        unloadWorld();
        if (auto back = loadWorld(leftPath, leaving, {}); !back)
        {
            G7_LOG_ERROR("engine", "cannot return to {}: {}", leftPath, back.error().message);
        }
        return;
    }
    m_triggers.setSpentTriggers(std::move(spent));
    releaseUnusedModels();
}

void Engine::releaseUnusedModels()
{
    std::set<const LoadedModel*> used;
    for (const SceneInstance& instance : m_instances)
    {
        used.insert(instance.model);
    }
    const usize before = m_models.size();
    std::erase_if(m_models, [&](const auto& entry) { return !used.contains(entry.second.get()); });
    if (m_models.size() != before)
    {
        G7_LOG_INFO("engine", "released {} models the new world does not use", before - m_models.size());
    }
}

Result<void> Engine::applyStartPoint(std::string_view name)
{
    auto start = world::findStartPoint(m_scene, name);
    if (!start)
    {
        return name.empty() ? Result<void>() : Result<void>(start.error()); // none: overview camera
    }
    const Mat4 world = m_scene.worldMatrix(start.value());
    m_camera.transform.position = Vec3(world[3]) + Vec3(0.0f, world::kStartEyeHeight, 0.0f);
    m_camera.transform.rotation = glm::normalize(glm::quat_cast(Mat3(
        glm::normalize(Vec3(world[0])), glm::normalize(Vec3(world[1])), glm::normalize(Vec3(world[2])))));
    m_flyCamera.speed = 10.0f;
    m_flyCamera.attach(m_camera);
    G7_LOG_INFO("engine", "start point {}", m_scene.get<world::Vob>(start.value())->nameText);
    return {};
}

void Engine::loadTerrainSurface(const world::TerrainRef& ref)
{
    render::TerrainSurfaceDesc surface;
    surface.holes = m_heightfield.holes();
    std::vector<asset::Handle<asset::TextureData>> maps;
    std::vector<asset::Handle<asset::TextureData>> albedos;
    for (const std::string& path : ref.splatMaps)
    {
        maps.push_back(m_assets->load<asset::TextureData>(preferCooked(m_vfs, path)));
    }
    for (const world::TerrainLayerRef& layer : ref.layers)
    {
        albedos.push_back(m_assets->load<asset::TextureData>(preferCooked(m_vfs, layer.albedo)));
    }
    m_assets->waitAll();
    std::string problem;
    const auto check = [&](const asset::Handle<asset::TextureData>& handle, std::string_view path)
    {
        if (!handle.isReady() && problem.empty())
        {
            problem = std::format("'{}': {}", path, handle.error());
        }
        return handle.get();
    };
    for (usize i = 0; i < maps.size(); ++i)
    {
        surface.splatMaps.push_back(check(maps[i], ref.splatMaps[i]));
    }
    for (usize i = 0; i < albedos.size(); ++i)
    {
        surface.layers.push_back({check(albedos[i], ref.layers[i].albedo), ref.layers[i].tile});
    }
    auto applied = problem.empty() ? m_terrain.setSurface(*m_device, surface) : Result<void>(Error{problem});
    if (!applied)
    {
        G7_LOG_WARN("engine", "terrain surface: {} - slope colours instead", applied.error().message);
        surface.splatMaps.clear();
        surface.layers.clear();
        applied = m_terrain.setSurface(*m_device, surface); // the holes alone
    }
    if (!applied)
    {
        G7_LOG_WARN("engine", "terrain holes: {}", applied.error().message);
        return;
    }
    if (m_terrain.layerCount() > 0 || m_terrain.hasHoles())
    {
        G7_LOG_INFO("engine", "terrain surface: {} layers, {}", m_terrain.layerCount(),
                    m_terrain.hasHoles() ? "with holes" : "no holes");
    }
}

Result<void> Engine::saveWorld(const fs::Path& path) const
{
    const std::string name = fs::toUtf8(path.stem()); // the file names the world
    // What the scene does not hold comes from the loaded world: terrain, waynet, zones, generator head.
    world::WorldFile file = m_worldFile;
    world::WorldFile captured = world::captureWorld(m_scene, name);
    file.name = name;
    file.nextVobId = captured.nextVobId;
    file.vobs = std::move(captured.vobs);
    if (m_hasTerrain)
    {
        file.terrain = m_heightfield.ref(); // the scene holds only vobs
    }
    if (auto written = fs::writeTextAtomic(path, world::writeWorldFile(file)); !written)
    {
        return Error{"cannot save world: " + written.error().message};
    }
    G7_LOG_INFO("engine", "saved world ({} vobs) to {}", m_scene.vobCount(), fs::toUtf8(path));
    return {};
}

void Engine::initEnvironment()
{
    // Day and night from data (data/environment.toml, world.md); the fixed dusk without the file.
    const std::string curves =
        m_config.settings.get<std::string>("time.environment", "data/environment.toml");
    if (auto cycle = world::DayCycle::load(m_vfs, curves))
    {
        m_dayCycle = std::move(cycle).value();
        G7_LOG_INFO("engine", "day and night from {} ({} keys)", curves, m_dayCycle.keys().size());
    }
    else
    {
        G7_LOG_WARN("engine", "no day and night ({}), fixed dusk instead", cycle.error().message);
        m_dayCycle = world::DayCycle::fallback();
    }
    m_sunEnabled = m_config.sun;
    // Low warm evening sun, cool ambient and fog in the horizon colour of the dusk background, until
    // the sky and time of day (M4) drive these.
    m_environment.sunDirection = Vec3(0.6f, 0.25f, 0.4f);
    m_environment.sunColor = Vec3(1.0f, 0.72f, 0.5f);
    m_environment.sunIntensity = m_config.sun ? kSunIntensity : 0.0f;
    m_environment.ambientSky = Vec3(0.16f, 0.18f, 0.26f);
    m_environment.ambientGround = Vec3(0.07f, 0.06f, 0.05f);
    m_environment.fogColor = Vec3(0.0844f, 0.0395f, 0.0331f); // sRGB (0.32, 0.22, 0.20), the dusk horizon
    m_environment.fogStart = static_cast<f32>(m_config.settings.get<f64>("render.fog_start", 30.0));
    // Default density: 90 % fog at 300 m.
    m_environment.fogDensity = static_cast<f32>(
        m_config.settings.get<f64>("render.fog_density", render::fogDensityFor(0.9f, 300.0f, 30.0f)));
    m_fogBaseDensity = m_environment.fogDensity;
    updateEnvironment();
}

void Engine::updateEnvironment()
{
    const f32 fogStart = m_environment.fogStart;
    const world::DaySample sample = m_dayCycle.evaluate(m_gameTime.hourOfDay(), m_fogBaseDensity);
    m_environment = sample.environment;
    m_environment.fogStart = fogStart;
    if (!m_sunEnabled)
    {
        m_environment.sunIntensity = 0.0f;
    }
    m_sky = sample.sky;
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
    updateEnvironment(); // light, fog and sky at the current game time

    // Scene into the linear HDR target: background, then meshes (after their shadow pass).
    drawScene(width, height);

    // Tonemap into the window.
    m_device->bindFramebuffer(nullptr);
    m_post.apply(*m_device, m_sceneTarget, width, height, m_postSettings);

    // Debug drawing on top, depth-tested against the scene.
    if (m_debugOverlay)
    {
        addDebugOverlay(width, height);
        m_debugRenderer.render(*m_device, m_debugDraw, m_camera, &m_sceneTarget.depth(), width, height);
    }
    // ImGui last, on top of everything.
    if (m_debugUiFrame)
    {
        m_debugUi.endFrame(m_device.get());
        m_debugUiFrame = false;
    }
}

void Engine::drawScene(u32 width, u32 height)
{
    m_meshRenderer.beginFrame();
    if (m_cullGridDirty)
    {
        std::vector<AABB> bounds;
        bounds.reserve(m_instances.size());
        for (const SceneInstance& instance : m_instances)
        {
            bounds.push_back(instance.bounds);
        }
        m_cullGrid.build(bounds);
        m_cullGridDirty = false;
    }
    // Shadow pass: every caster whose bounds reach a cascade's light volume (which extends towards
    // the sun, so casters outside the view still count). The flat ground plate cannot shadow
    // anything; terrain can (hills shade valleys).
    render::ShadowFrame shadowFrame;
    if ((!m_instances.empty() || m_hasTerrain) && m_environment.sunIntensity > 0.0f)
    {
        m_cascades = render::computeCascades(m_camera, m_environment.sunDirection, m_shadowMap.settings());
        m_shadowMap.begin(*m_device);
        for (u32 i = 0; i < m_cascades.size(); ++i)
        {
            m_shadowMap.beginCascade(*m_device, i);
            const Frustum volume = Frustum::fromViewProjection(m_cascades[i].viewProjection);
            m_cullCandidates.clear();
            m_cullGrid.query(volume, m_camera.transform.position, 0.0f, m_cullCandidates);
            m_drawItems.clear();
            for (const u32 index : m_cullCandidates)
            {
                // What the main pass hides (distance, size) casts no shadow either.
                const SceneInstance& instance = m_instances[index];
                if (instance.model != m_groundModel.get() && volume.intersects(instance.bounds) &&
                    render::cullByDistance(instance.bounds, m_camera.transform.position, m_cullSettings,
                                           instance.sizeCullable) == render::CullResult::Kept)
                {
                    if (m_multiDraw)
                    {
                        m_drawItems.push_back({&instance.model->mesh, &instance.model->materials,
                                               instance.transform, instance.bounds});
                    }
                    else
                    {
                        m_meshRenderer.drawShadow(*m_device, instance.model->mesh, instance.model->materials,
                                                  instance.transform, m_cascades[i]);
                    }
                }
            }
            if (m_multiDraw)
            {
                m_meshRenderer.drawShadowBatched(*m_device, m_drawItems, m_cascades[i]);
            }
            if (m_hasTerrain)
            {
                m_terrain.drawShadow(*m_device, m_cascades[i]);
            }
        }
        shadowFrame = {&m_shadowMap, m_cascades, &m_camera, m_shadowDebug};
    }

    m_device->bindFramebuffer(&m_sceneTarget.framebuffer());
    m_device->setViewport(0, 0, width, height);
    m_device->clear(Vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    m_backgroundProgram->setUniform("uInverseViewProjection", glm::inverse(m_camera.viewProjection()));
    m_backgroundProgram->setUniform("uCameraPosition", m_camera.transform.position);
    m_backgroundProgram->setUniform("uHorizonColor", m_environment.fogColor);
    m_backgroundProgram->setUniform("uZenithColor", m_sky.zenith);
    m_backgroundProgram->setUniform("uSunDirection", m_sky.sunDirection);
    m_backgroundProgram->setUniform("uSunColor", m_sky.sunColor);
    m_backgroundProgram->setUniform("uMoonDirection", m_sky.moonDirection);
    m_backgroundProgram->setUniform("uMoon", m_sky.moon);
    m_backgroundProgram->setUniform("uStars", m_sky.stars);
    m_device->bindPipeline(m_backgroundPipeline);
    m_device->draw(3); // fullscreen triangle from gl_VertexID
    if (m_instances.empty() && !m_hasTerrain)
    {
        return;
    }

    // Main pass: terrain (culled and detailed per chunk), then the instances inside the view frustum.
    m_meshRenderer.setLighting(*m_device, m_environment, m_lights, shadowFrame.map ? &shadowFrame : nullptr);
    if (m_hasTerrain)
    {
        m_meshRenderer.bindLighting(*m_device);
        m_terrain.draw(*m_device, m_camera, &m_lights);
    }
    const Frustum view = m_camera.frustum();
    m_visibleInstances = 0;
    m_culledFar = 0;
    m_culledSmall = 0;
    m_cullCandidates.clear();
    m_cullGrid.query(view, m_camera.transform.position, m_cullSettings.viewDistance, m_cullCandidates);
    m_drawItems.clear();
    for (const u32 index : m_cullCandidates)
    {
        const SceneInstance& instance = m_instances[index];
        const render::CullResult cull = render::cullByDistance(instance.bounds, m_camera.transform.position,
                                                               m_cullSettings, instance.sizeCullable);
        m_culledFar += cull == render::CullResult::TooFar ? 1 : 0;
        m_culledSmall += cull == render::CullResult::TooSmall ? 1 : 0;
        if (cull == render::CullResult::Kept && view.intersects(instance.bounds))
        {
            if (m_multiDraw)
            {
                m_drawItems.push_back(
                    {&instance.model->mesh, &instance.model->materials, instance.transform, instance.bounds});
            }
            else
            {
                m_meshRenderer.draw(*m_device, instance.model->mesh, instance.model->materials,
                                    instance.transform, m_camera);
            }
            ++m_visibleInstances;
        }
    }
    if (m_multiDraw)
    {
        m_meshRenderer.drawBatched(*m_device, m_drawItems, m_camera);
    }
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

    // Mouse look while the right mouse button is held (relative mode hides and captures the cursor).
    // It starts only outside the debug UI but, once started, continues over it.
    if (allowMouse && m_input.pressed(platform::MouseButton::Right))
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

void Engine::updateBenchmark(f64 realSeconds)
{
    // Each viewpoint for benchmarkFrames frames; the first 10 % (at most 30) warm up and are not
    // measured. `realSeconds` is the duration of the previous frame, so the first frame of a
    // viewpoint (which measures the old one) is skipped too.
    if (m_viewpoints.empty())
    {
        m_viewpoints.push_back({m_camera.transform.position, m_flyCamera.yaw(), m_flyCamera.pitch()});
    }
    const u64 perView = std::max<u64>(m_config.benchmarkFrames, 2);
    const u64 warmup = std::min<u64>(perView / 10, 30);
    const u64 view = m_benchmarkFrame / perView;
    const u64 frameInView = m_benchmarkFrame % perView;
    ++m_benchmarkFrame;
    if (view >= m_viewpoints.size())
    {
        if (!m_quitRequested)
        {
            for (const FrameTimeSummary& s : m_benchmarkResults)
            {
                const auto v = static_cast<usize>(&s - m_benchmarkResults.data());
                const render::FrameStats& stats = m_benchmarkStats[v];
                const render::BatchStats& batch = m_benchmarkBatches[v];
                G7_LOG_INFO(
                    "engine",
                    "benchmark viewpoint {}: {}; {} draws, {} buffer binds, {} pipeline changes, {}k tris; "
                    "main pass: {} batches ({} submeshes), {} single draws, {} terrain chunks",
                    v, s.toString(), stats.drawCalls, stats.bufferBinds, stats.pipelineChanges,
                    stats.triangles / 1000, batch.groups, batch.batchedDraws, batch.singleDraws,
                    m_benchmarkTerrainChunks[v]);
            }
            const auto [worstAverage, worstP99] = std::accumulate(
                m_benchmarkResults.begin(), m_benchmarkResults.end(), std::pair{0.0, 0.0},
                [](auto acc, const auto& s)
                { return std::pair{std::max(acc.first, s.averageMs), std::max(acc.second, s.p99Ms)}; });
            G7_LOG_INFO("engine",
                        "benchmark on {}: slowest viewpoint {:.2f} ms avg ({:.0f} fps), p99 {:.2f} ms",
                        m_device ? m_device->info().renderer : "no device", worstAverage,
                        worstAverage > 0.0 ? 1000.0 / worstAverage : 0.0, worstP99);
            requestQuit();
        }
        return;
    }
    if (frameInView == 0)
    {
        setViewpoint(m_viewpoints[view]);
        m_benchmarkTimes.clear();
    }
    else if (frameInView > warmup)
    {
        m_benchmarkTimes.add(realSeconds);
    }
    if (frameInView + 1 == perView)
    {
        m_benchmarkResults.push_back(m_benchmarkTimes.summary());
        m_benchmarkStats.push_back(m_device ? m_device->stats() : render::FrameStats{});
        m_benchmarkBatches.push_back(m_meshRenderer.lastBatch());
        m_benchmarkTerrainChunks.push_back(m_hasTerrain ? m_terrain.drawnChunks() : 0);
    }
}

void Engine::saveScreenshot(u32 width, u32 height)
{
    // The window's back buffer before the swap: RGBA, bottom row first.
    std::vector<u8> pixels = m_device->readPixels(0, 0, static_cast<i32>(width), static_cast<i32>(height));
    asset::ImageData image{width, height, {}};
    image.rgba8.resize(pixels.size());
    const usize row = static_cast<usize>(width) * 4;
    for (u32 y = 0; y < height; ++y)
    {
        std::copy_n(pixels.data() + (height - 1 - y) * row, row, image.rgba8.data() + y * row);
    }
    for (usize i = 3; i < image.rgba8.size(); i += 4)
    {
        image.rgba8[i] = 255; // the window's alpha is meaningless
    }
    if (auto saved = asset::savePng(m_config.screenshot, image); !saved)
    {
        G7_LOG_ERROR("engine", "screenshot: {}", saved.error().message);
        return;
    }
    G7_LOG_INFO("engine", "screenshot saved to {}", fs::toUtf8(m_config.screenshot));
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

void Engine::setDebugUiVisible(bool visible) noexcept
{
    if (visible != m_debugUiVisible)
    {
        m_debugUiVisible = visible;
        G7_LOG_INFO("engine", "debug UI {}", visible ? "on" : "off");
    }
}

void Engine::runDebugUi(f64 realSeconds)
{
    m_debugUiFrame = m_debugUiVisible && m_debugUi.valid();
    m_window->setTextInput(m_debugUiFrame && m_debugUi.wantsText());
    if (!m_debugUiFrame)
    {
        return;
    }
    const auto size = m_window->size();
    const auto pixels = m_window->pixelSize();
    m_debugUi.beginFrame(m_input, Vec2(static_cast<f32>(size.width), static_cast<f32>(size.height)),
                         Vec2(static_cast<f32>(pixels.width), static_cast<f32>(pixels.height)),
                         static_cast<f32>(realSeconds));

    // Statistics are from the previous frame (this one is not rendered yet).
    ui::EnginePanel panel;
    panel.frame = m_device->stats();
    panel.cameraPosition = m_camera.transform.position;
    panel.width = pixels.width;
    panel.height = pixels.height;
    panel.simulationTicks = m_simTicks;
    panel.fovDegrees = toDegrees(m_camera.fovY);
    panel.flySpeed = m_flyCamera.speed;
    panel.tonemapper = m_postSettings.tonemapper;
    panel.exposure = m_postSettings.exposure;
    panel.fogStart = m_environment.fogStart;
    panel.fogDensity = m_fogBaseDensity;
    panel.sun = m_sunEnabled;
    panel.hour = m_gameTime.hourOfDay();
    panel.minuteSeconds = static_cast<f32>(m_gameTime.secondsPerMinute());
    const f32 shownHour = panel.hour;
    panel.shadowDebug = m_shadowDebug;
    panel.debugDraw = m_debugOverlay;
    panel.paused = m_paused;
    panel.timeScale = static_cast<f32>(m_timeScale);
    m_debugUi.enginePanel(panel);
    for (EngineTool* tool : m_tools)
    {
        tool->ui(*this, m_debugUi);
    }

    m_camera.fovY = toRadians(panel.fovDegrees);
    m_flyCamera.speed = panel.flySpeed;
    m_postSettings.tonemapper = panel.tonemapper;
    m_postSettings.exposure = panel.exposure;
    m_environment.fogStart = panel.fogStart;
    m_fogBaseDensity = panel.fogDensity;
    m_sunEnabled = panel.sun;
    if (panel.hour != shownHour)
    {
        const auto minutes = static_cast<u32>(panel.hour * 60.0f);
        m_gameTime.setTime(m_gameTime.day(), minutes / 60 % 24, minutes % 60);
    }
    m_gameTime.setSecondsPerMinute(panel.minuteSeconds);
    m_shadowDebug = panel.shadowDebug;
    setDebugOverlay(panel.debugDraw);
    setPaused(panel.paused);
    setTimeScale(panel.timeScale);
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
    m_debugDraw.screenText(Vec2(8.0f, 8.0f),
                           std::format("{:.0f} fps  {:.2f} ms{}\n{} draws  {} binds  {:.1f}k tris  {}/{} "
                                       "objects (hidden: {} far, {} small)\n{}x{}  cam {:.1f} {:.1f} {:.1f}  "
                                       "day {} {:02}:{:02}",
                                       ms > 0.0 ? 1000.0 / ms : 0.0, ms, m_paused ? "  PAUSED" : "",
                                       stats.drawCalls, stats.bufferBinds, stats.triangles / 1000.0,
                                       m_visibleInstances, m_instances.size(), m_culledFar, m_culledSmall,
                                       width, height, p.x, p.y, p.z, m_gameTime.day(),
                                       static_cast<u32>(m_gameTime.minuteOfDay()) / 60,
                                       static_cast<u32>(m_gameTime.minuteOfDay()) % 60),
                           Vec4(1.0f), 2.0f);

    // World origin and the scene: ground grid, bounds (with the name for a single model), torches.
    m_debugDraw.axes(Mat4(1.0f), 1.0f);
    addWorldDebugOverlay();
    if (m_sceneBounds.max.x < m_sceneBounds.min.x)
    {
        return;
    }
    const AABB& bounds = m_sceneBounds;
    const f32 size = std::max(glm::length(bounds.max - bounds.min), 1.0f);
    const f32 spacing = std::exp2(std::round(std::log2(size / 8.0f))); // ~8 cells across the scene
    m_debugDraw.grid(Vec3(0.0f, bounds.min.y, 0.0f), spacing * 40.0f, spacing,
                     {Vec4(0.6f, 0.6f, 0.6f, 0.35f)});
    const Vec4 yellow(1.0f, 0.85f, 0.2f, 1.0f);
    if (m_config.scene.empty())
    {
        m_debugDraw.box(bounds, {yellow});
        m_debugDraw.text(Vec3(bounds.center().x, bounds.max.y, bounds.center().z) +
                             Vec3(0.0f, size * 0.08f, 0.0f),
                         m_sceneName, {yellow}, 2.0f);
    }
    else
    {
        const Frustum view = m_camera.frustum();
        for (const SceneInstance& instance : m_instances)
        {
            if (instance.model != m_groundModel.get() && view.intersects(instance.bounds))
            {
                m_debugDraw.box(instance.bounds, {Vec4(1.0f, 0.85f, 0.2f, 0.25f)});
            }
        }
    }
    for (const render::PointLight& light : m_lights.lights())
    {
        const render::DebugStyle style{Vec4(1.0f, 0.55f, 0.2f, 0.8f)};
        m_debugDraw.cross(light.position, std::min(size * 0.05f, 0.5f), style);
        m_debugDraw.sphere(light.position, light.radius, style);
    }
}

void Engine::addWorldDebugOverlay()
{
    // Vobs that draw nothing themselves: start points, triggers, sounds; mobs get their definition.
    const Vec4 green(0.3f, 1.0f, 0.4f, 1.0f);
    m_scene.each<world::Vob, world::StartPoint, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::StartPoint&, const world::WorldTransform& t)
        {
            const Vec3 feet(t.matrix[3]);
            const Vec3 forward = glm::normalize(Vec3(t.matrix * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            m_debugDraw.cross(feet, 0.3f, {green});
            m_debugDraw.line(feet, feet + Vec3(0.0f, world::kStartEyeHeight, 0.0f), {green});
            m_debugDraw.arrow(feet + Vec3(0.0f, world::kStartEyeHeight, 0.0f),
                              feet + Vec3(0.0f, world::kStartEyeHeight, 0.0f) + forward, {green});
            m_debugDraw.text(feet + Vec3(0.0f, world::kStartEyeHeight + 0.4f, 0.0f), vob.nameText, {green});
        });
    m_scene.each<world::Vob, world::TriggerVolume, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::TriggerVolume& volume,
            const world::WorldTransform& t)
        {
            const bool inside = m_triggers.isInside(vob.id, kCameraProbe);
            const render::DebugStyle style{inside ? Vec4(1.0f, 0.3f, 0.3f, 1.0f)
                                                  : Vec4(0.4f, 0.7f, 1.0f, 0.8f)};
            if (volume.shape == world::TriggerVolume::Shape::Box)
            {
                m_debugDraw.box(t.matrix, volume.halfExtents, style);
            }
            else
            {
                const f32 scale = std::max({glm::length(Vec3(t.matrix[0])), glm::length(Vec3(t.matrix[1])),
                                            glm::length(Vec3(t.matrix[2]))});
                m_debugDraw.sphere(Vec3(t.matrix[3]), volume.radius * scale, style);
            }
            m_debugDraw.text(Vec3(t.matrix[3]), vob.nameText, style);
        });
    m_scene.each<world::Vob, world::SoundEmitter, world::WorldTransform>(
        [&](entt::entity, const world::Vob&, const world::SoundEmitter& sound, const world::WorldTransform& t)
        {
            const render::DebugStyle style{Vec4(0.8f, 0.5f, 1.0f, 0.6f)};
            m_debugDraw.cross(Vec3(t.matrix[3]), 0.3f, style);
            m_debugDraw.circle(Vec3(t.matrix[3]), Vec3(0.0f, 1.0f, 0.0f), sound.range, style);
            m_debugDraw.text(Vec3(t.matrix[3]) + Vec3(0.0f, 0.4f, 0.0f), sound.sound, style);
        });
    m_scene.each<world::Vob, world::MobRef, world::WorldTransform>(
        [&](entt::entity, const world::Vob&, const world::MobRef& mob, const world::WorldTransform& t)
        {
            const render::DebugStyle style{Vec4(1.0f, 0.85f, 0.2f, 1.0f)};
            m_debugDraw.cross(Vec3(t.matrix[3]) + Vec3(0.0f, 1.0f, 0.0f), 0.2f, style); // focus point (M8)
            m_debugDraw.text(Vec3(t.matrix[3]) + Vec3(0.0f, 1.3f, 0.0f), mob.definition, style);
        });
}

void Engine::shutdown()
{
    if (!m_initialized)
    {
        return;
    }
    // Shutdown in reverse init order.
    G7_LOG_INFO("engine", "shutdown");
    m_debugUi = {}; // releases its GL textures
    m_debugRenderer = {};
    m_debugDraw.clear();
    m_terrain = {};
    m_meshRenderer = {};
    m_post = {};
    m_sceneTarget = {};
    m_shadowMap = {};
    m_instances.clear();
    m_groundModel.reset();
    m_models.clear();
    m_geometry.reset(); // after every mesh that lives in it
    m_assets.reset();   // before the VFS (a member destroyed after it)
    m_backgroundPipeline = {};
    m_backgroundProgram = nullptr;
    m_shaders.reset();
    m_device.reset(); // GL objects need the context
    m_glContext.reset();
    m_window.reset();
    m_initialized = false;
}
} // namespace g7

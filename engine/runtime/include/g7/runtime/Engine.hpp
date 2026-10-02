#pragma once

// Module: g7::runtime
// Verbindet alle Engine-Module zu einer lauffaehigen Anwendung: Initialisierungsreihenfolge,
// Hauptschleife (fester Simulationsschritt + interpoliertes Rendern), Shutdown.
// Spezifikation: docs/02-architecture.md ("Hauptschleife", "Initialisierung")

#include <g7/asset/AssetManager.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/core/Clock.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/platform/Actions.hpp>
#include <g7/platform/GlContext.hpp>
#include <g7/platform/Input.hpp>
#include <g7/platform/Window.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/runtime/FrameTimes.hpp>
#include <g7/runtime/SceneFile.hpp>
#include <g7/ui/DebugUi.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/WorldFile.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace g7
{
/// A model on the GPU, shared by all its instances, with the asset handles it was built from.
struct LoadedModel
{
    render::Mesh mesh;
    render::MaterialSet materials;
    std::string name; ///< VFS path
    asset::Handle<asset::MeshData> source;
    std::vector<asset::Handle<asset::TextureData>> images; ///< parallel to MeshData::images (external ones)
    u32 sourceVersion = 0; ///< versions uploaded to the GPU (hot reload compares them)
    std::vector<u32> imageVersions;
};

/// One placed model with its world bounds (for culling).
struct SceneInstance
{
    const LoadedModel* model = nullptr;
    Mat4 transform{1.0f};
    AABB bounds;
};

struct EngineConfig
{
    std::string appName = "Gothar";
    f64 simulationHz = 60.0; ///< Fixed simulation rate.
    u64 maxFrames = 0;       ///< 0 = unlimited. Used by tests and headless runs.
    f64 maxFps = 0.0;        ///< Frame-rate cap with a window; 0 = unlimited. Headless is never capped.
    /// > 0: every frame advances exactly this much time instead of real time (deterministic
    /// tests and CI runs).
    f64 fixedFrameSeconds = 0.0;
    bool headless = false; ///< No window (tests, CI smoke test).
    bool render = true;    ///< OpenGL rendering in the window (false: window without GL, --no-render).
    /// Engine shaders; empty = gamePath("shaders"). Point it at engine/render/shaders to edit the
    /// sources live with hot-reload ([render] shader_dir).
    fs::Path shaderDirectory;
    /// Optional model shown at the origin (--view-mesh, glTF or .g7mesh); the debug camera frames it.
    /// A VFS path, or a file on disk whose folder is then mounted under "local/".
    fs::Path viewMesh;
    bool sun = true; ///< false: no sunlight (--no-sun), to judge point lights alone.
    /// Optional test scene (--scene, format in SceneFile.hpp); replaces --view-mesh. VFS path or file
    /// on disk like viewMesh.
    fs::Path scene;
    /// Optional world (--world, .g7world); VFS path or file on disk like viewMesh. Takes precedence
    /// over scene and viewMesh.
    fs::Path world;
    /// After loading, write the scene's world vobs as .g7world to this file (--save-world).
    fs::Path saveWorld;
    u32 viewpoint = 0;  ///< Start viewpoint of the scene (--viewpoint=N).
    bool ground = true; ///< Ground plate under the --view-mesh model (--no-ground).
    /// Benchmark (--benchmark): visits the scene's viewpoints for `benchmarkFrames` frames each, logs
    /// frame-time statistics and quits. The caller turns VSync and the frame cap off.
    bool benchmark = false;
    u32 benchmarkFrames = 300;
    /// Saves the last rendered frame as PNG (--screenshot=<file>), e.g. with --frames or --benchmark.
    fs::Path screenshot;
    platform::WindowDesc window; ///< Used unless headless.
    /// Merged settings (engine.toml + user config). The engine reads [input] (scheme,
    /// stick_deadzone) and [bindings.<scheme>]; window settings are applied by the caller.
    Config settings;
};

class Engine
{
public:
    explicit Engine(EngineConfig config);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Initializes all subsystems in dependency order.
    [[nodiscard]] Result<void> init();

    /// Runs the main loop until quit is requested or maxFrames is reached.
    /// Returns the process exit code.
    int run();
    /// Runs a single frame (events, simulation steps, render, frame cap). Returns false once
    /// quitting was requested. run() is a loop over this; tests use it to step frame by frame.
    [[nodiscard]] bool runFrame();

    void requestQuit() noexcept { m_quitRequested = true; }

    [[nodiscard]] u64 frameCount() const noexcept { return m_frameCount; }
    [[nodiscard]] u64 simulationTicks() const noexcept { return m_simTicks; }
    /// Simulated time in seconds (ticks * step): stands still while paused, follows timeScale.
    [[nodiscard]] f64 simulationTime() const noexcept
    {
        return static_cast<f64>(m_simTicks) * m_fixedStep.step();
    }
    /// Interpolation factor between the last two simulation states, for rendering.
    [[nodiscard]] f64 frameAlpha() const noexcept { return m_fixedStep.alpha(); }

    /// Speed of simulated time relative to real time, clamped to [0, kMaxTimeScale].
    void setTimeScale(f64 scale) noexcept;
    [[nodiscard]] f64 timeScale() const noexcept { return m_timeScale; }
    /// Paused: events and rendering continue, the simulation does not advance and does not
    /// catch up afterwards.
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept { return m_paused; }
    static constexpr f64 kMaxTimeScale = 10.0;

    /// Virtual file system with the mounts of [assets] (docs/modules/asset.md).
    [[nodiscard]] asset::Vfs& vfs() noexcept { return m_vfs; }
    /// Asset loading through the VFS; update() runs once per frame.
    [[nodiscard]] asset::AssetManager& assets() noexcept { return *m_assets; }
    /// A loaded model by its VFS path (as first requested), or nullptr.
    [[nodiscard]] const LoadedModel* model(std::string_view path) const;
    /// Render device, or nullptr without rendering.
    [[nodiscard]] render::Device* renderDevice() noexcept { return m_device.get(); }
    /// Placed objects of the scene (--scene / --view-mesh, ground included) and how many were drawn in
    /// the last frame after frustum culling.
    [[nodiscard]] usize sceneObjectCount() const noexcept { return m_instances.size(); }
    [[nodiscard]] u32 visibleSceneObjects() const noexcept { return m_visibleInstances; }
    /// Vobs of the loaded world or scene.
    [[nodiscard]] world::Scene& scene() noexcept { return m_scene; }
    /// Camera used for rendering (a free-flying debug camera until the player exists, M5).
    [[nodiscard]] render::Camera& camera() noexcept { return m_camera; }
    /// Shader programs (with hot-reload), or nullptr without rendering.
    [[nodiscard]] render::ShaderLibrary* shaders() noexcept { return m_shaders.get(); }
    /// The game window, or nullptr when headless / before init().
    [[nodiscard]] platform::Window* window() noexcept { return m_window.get(); }
    /// Input state of the current frame (stays empty when headless).
    [[nodiscard]] const platform::Input& input() const noexcept { return m_input; }
    /// Bindings of the active control scheme.
    [[nodiscard]] const platform::ActionMap& actions() const noexcept { return m_actions; }
    /// Debug lines, shapes and text (drawn while the overlay is on, toggled with the debug_draw action).
    [[nodiscard]] render::DebugDraw& debugDraw() noexcept { return m_debugDraw; }
    [[nodiscard]] bool debugOverlay() const noexcept { return m_debugOverlay; }
    void setDebugOverlay(bool enabled) noexcept;
    /// ImGui debug panels (toggled with the debug_ui action, F1); only with rendering.
    [[nodiscard]] bool debugUiVisible() const noexcept { return m_debugUiVisible; }
    void setDebugUiVisible(bool visible) noexcept;

private:
    void shutdown();
    [[nodiscard]] Result<void> initShaders();
    [[nodiscard]] Result<void> initSceneRendering();
    [[nodiscard]] Result<void> initViewMesh();
    [[nodiscard]] Result<void> initScene();
    [[nodiscard]] Result<void> initWorld();
    /// Render instances and lights for the vobs in m_scene (mesh and light vobs).
    [[nodiscard]] Result<void> instantiateScene();
    [[nodiscard]] Result<void> saveWorld(const fs::Path& path) const;
    [[nodiscard]] Result<void> initAssets();
    /// VFS path for a --scene/--view-mesh argument (mounting the folder of a disk file under local/).
    [[nodiscard]] Result<std::string> resolveAssetArgument(const fs::Path& argument);
    /// Loads models (meshes and their images) through the asset manager in one batch and uploads
    /// them; models already loaded are kept.
    [[nodiscard]] Result<void> loadModels(const std::vector<std::string>& paths);
    void requestImages(LoadedModel& model, std::vector<std::string>* imagePaths);
    [[nodiscard]] Result<void> uploadModel(LoadedModel& model);
    void refreshReloadedModels();
    [[nodiscard]] Result<void> addGround(f32 size, const Vec3& color, f32 height);
    void addInstance(const LoadedModel& model, const Mat4& transform);
    void setViewpoint(const SceneViewpoint& viewpoint);
    void updateBenchmark(f64 realSeconds);
    void saveScreenshot(u32 width, u32 height);
    void initEnvironment();
    void renderScene(u32 width, u32 height);
    void drawScene(u32 width, u32 height);
    void addDebugOverlay(u32 width, u32 height);
    /// `allowMouse` / `allowKeyboard` false while the debug UI uses them.
    void updateDebugCamera(f64 realSeconds, bool allowMouse, bool allowKeyboard);
    void runDebugUi(f64 realSeconds);

    EngineConfig m_config;
    // Assets: the manager is destroyed before the VFS it reads from (member order).
    asset::Vfs m_vfs;
    std::unique_ptr<asset::AssetManager> m_assets;
    std::unique_ptr<platform::Window> m_window;
    std::unique_ptr<platform::GlContext> m_glContext; // must outlive m_device
    std::unique_ptr<render::Device> m_device;
    std::unique_ptr<render::ShaderLibrary> m_shaders; // destroyed before the device
    render::rhi::Pipeline m_backgroundPipeline;
    render::rhi::ShaderProgram* m_backgroundProgram = nullptr;
    render::SceneTarget m_sceneTarget; // linear HDR scene, tonemapped by m_post
    render::PostProcess m_post;
    render::PostSettings m_postSettings;
    render::DebugDraw m_debugDraw;
    render::DebugDrawRenderer m_debugRenderer;
    bool m_debugOverlay = false;
    ui::DebugUi m_debugUi; // invalid without rendering
    bool m_debugUiVisible = false;
    bool m_debugUiFrame = false;      // an ImGui frame was begun this frame and awaits rendering
    f64 m_frameSeconds = 0.0;         // real duration of the last frame
    f64 m_smoothedFrameSeconds = 0.0; // for the overlay's FPS display
    // Scene: --view-mesh (one model) or --scene (test scene); models are shared by instances.
    world::Scene m_scene; // world vobs (--world, --scene); render instances are built from it
    std::map<std::string, std::unique_ptr<LoadedModel>, std::less<>> m_models; // by VFS path
    std::unique_ptr<LoadedModel> m_groundModel;
    std::vector<SceneInstance> m_instances;
    std::string m_sceneName;
    AABB m_sceneBounds{Vec3(1.0f), Vec3(-1.0f)}; // empty until the first non-ground instance
    std::vector<SceneViewpoint> m_viewpoints;
    u32 m_visibleInstances = 0;
    FrameTimes m_benchmarkTimes;
    std::vector<FrameTimeSummary> m_benchmarkResults;
    u64 m_benchmarkFrame = 0;
    render::MeshRenderer m_meshRenderer; // pipelines reference ShaderLibrary programs
    render::Environment m_environment;
    render::LightList m_lights;
    render::ShadowMap m_shadowMap;
    std::vector<render::Cascade> m_cascades;
    bool m_shadowDebug = false;
    render::Camera m_camera;
    render::FreeFlyCamera m_flyCamera;
    bool m_mouseLook = false;
    platform::Input m_input;
    platform::ActionMap m_actions;
    FixedStep m_fixedStep{1.0 / 60.0};
    FramePacer m_framePacer{0.0};
    Stopwatch m_frameTimer;
    f64 m_timeScale = 1.0;
    bool m_paused = false;
    bool m_initialized = false;
    bool m_quitRequested = false;
    u64 m_frameCount = 0;
    u64 m_simTicks = 0;
};
} // namespace g7

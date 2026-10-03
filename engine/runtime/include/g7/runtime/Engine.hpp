#pragma once

// Module: g7::runtime
// Verbindet alle Engine-Module zu einer lauffaehigen Anwendung: Initialisierungsreihenfolge,
// Hauptschleife (fester Simulationsschritt + interpoliertes Rendern), Shutdown.
// Spezifikation: docs/02-architecture.md ("Hauptschleife", "Initialisierung")

#include <g7/asset/AssetManager.hpp>
#include <g7/asset/FigureAssembly.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/core/Clock.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/physics/Character.hpp>
#include <g7/physics/Physics.hpp>
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
#include <g7/render/Terrain.hpp>
#include <g7/render/Visibility.hpp>
#include <g7/runtime/EngineTool.hpp>
#include <g7/runtime/FrameTimes.hpp>
#include <g7/runtime/SceneFile.hpp>
#include <g7/runtime/StartView.hpp>
#include <g7/script/ScriptVm.hpp>
#include <g7/ui/DebugUi.hpp>
#include <g7/world/DayCycle.hpp>
#include <g7/world/GameTime.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/Terrain.hpp>
#include <g7/world/Triggers.hpp>
#include <g7/world/Water.hpp>
#include <g7/world/WorldFile.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
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
    AABB bounds;      ///< model space, from the CPU data (also without a GPU: --no-render)
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
    bool sizeCullable = true; ///< deco: may vanish when small on screen (render.md "Sichtbarkeit")
    world::VobId vob;         ///< the mesh vob drawn (0: ground plate, --view-mesh, test scenes)
    bool solid = true;        ///< collides and casts shadows (false: the water surface placeholder)
};

/// A landing of the player: fall height from the highest point and the hit points it cost.
struct PlayerLanding
{
    f32 height = 0.0f;
    f32 damage = 0.0f;
    bool intoWater = false;
    Vec3 feet{0.0f};
    u64 tick = 0; ///< simulation tick of the landing
};

namespace animation
{
struct AnimGraph;
}
namespace script
{
class ScriptVm;
class Value;
} // namespace script
struct AnimatedFigure; // animated figures (EngineFigure.cpp)
struct PlayerFigure;   // the animated hero
struct Creature;       // an animal (EngineCreatures.cpp)

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
    u32 viewpoint = 0; ///< Start viewpoint of the scene (--viewpoint=N).
    /// Start point of the world by name (--start); empty = the one with the lowest id.
    std::string start;
    /// Game time at start, "HH:MM" (--time); empty = [time] start (default 08:00).
    std::string startTime;
    /// --cam, --yaw, --pitch, --fly, --player: a view to reproduce (StartView.hpp), applied after loading.
    StartView view;
    bool ground = true; ///< Ground plate under the --view-mesh model (--no-ground).
    /// Player figure at the start point of a loaded world (M5); off in the editor (--editor) and with
    /// --benchmark, which drive the camera themselves.
    bool player = true;
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
    /// Objects the last frame hid for their distance (view_distance) or size (size_cull) - counted among
    /// the objects of the grid cells it visited (whole cells beyond view_distance are skipped uncounted).
    [[nodiscard]] u32 culledByDistance() const noexcept { return m_culledFar; }
    [[nodiscard]] u32 culledBySize() const noexcept { return m_culledSmall; }
    [[nodiscard]] const render::CullSettings& cullSettings() const noexcept { return m_cullSettings; }
    /// Multi-draw batches (engine.toml [render] multi_draw, default on); off draws every mesh on its own.
    void setMultiDraw(bool enabled) noexcept { m_multiDraw = enabled; }
    [[nodiscard]] bool multiDraw() const noexcept { return m_multiDraw; }
    /// Batches of the last main pass.
    [[nodiscard]] const render::BatchStats& lastBatch() const noexcept { return m_meshRenderer.lastBatch(); }
    render::CullSettings& cullSettings() noexcept { return m_cullSettings; }
    /// Terrain of the loaded world, or nullptr; chunks drawn in the last frame.
    [[nodiscard]] const world::Heightfield* terrain() const noexcept
    {
        return m_hasTerrain ? &m_heightfield : nullptr;
    }
    [[nodiscard]] u32 visibleTerrainChunks() const noexcept
    {
        return m_hasTerrain ? m_terrain.drawnChunks() : 0;
    }
    /// Vobs of the loaded world or scene.
    [[nodiscard]] world::Scene& scene() noexcept { return m_scene; }
    /// Camera used for rendering (a free-flying debug camera until the player exists, M5).
    [[nodiscard]] render::Camera& camera() noexcept { return m_camera; }
    /// Level change: between this frame and the next, the current world is kept as it is (for coming back)
    /// and `world` (VFS path of a .g7world) is loaded with the camera on its start point `start`. An unknown
    /// world or start point is a warning and nothing changes. Game time stays (it is not part of a world).
    void requestWorldChange(std::string world, std::string start);
    /// Game time (global across worlds): day, time of day, speed; jumps with setTime/advanceTo.
    [[nodiscard]] world::GameTime& gameTime() noexcept { return m_gameTime; }
    /// Light, fog and sky of the last frame (from the day cycle).
    [[nodiscard]] const render::Environment& environment() const noexcept { return m_environment; }
    [[nodiscard]] const render::Sky& sky() const noexcept { return m_sky; }
    /// Writes the scene's world vobs with the loaded world's terrain, waynet, zones and generator head as
    /// .g7world (--save-world, editor).
    [[nodiscard]] Result<void> saveWorld(const fs::Path& path) const;
    /// Registers a tool (the editor) that runs every frame; it must outlive the engine's frames.
    void addTool(EngineTool& tool);
    /// After the scene changed (editor): render instances and lights rebuilt from it (models cached).
    [[nodiscard]] Result<void> refreshScene();
    /// The loaded world without its vobs (terrain, waynet, zones, generator head).
    [[nodiscard]] const world::WorldFile& worldFile() const noexcept { return m_worldFile; }
    /// Disk file of the loaded world when it comes from a loose folder (editor saving); nullopt for
    /// archives and scenes.
    [[nodiscard]] std::optional<fs::Path> worldSourceFile() const;
    /// Render instances (one per drawn mesh vob) - picking in the editor.
    [[nodiscard]] std::span<const SceneInstance> instances() const noexcept { return m_instances; }
    /// VFS path of the loaded world (--world or the last level change); empty for scenes and models.
    [[nodiscard]] const std::string& worldPath() const noexcept { return m_worldPath; }
    /// Worlds left during this session whose state is kept (lower-case paths).
    [[nodiscard]] usize keptWorlds() const noexcept { return m_leftWorlds.size(); }
    [[nodiscard]] usize loadedModels() const noexcept { return m_models.size(); }
    /// Trigger volumes of the world (fed with the camera until the player exists).
    [[nodiscard]] const world::TriggerSystem& triggers() const noexcept { return m_triggers; }
    /// Who the camera is for the triggers.
    static constexpr world::VobId kCameraProbe{~0ull};
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
    /// Collision of the loaded world (terrain, mesh vobs, ground plate) for queries. Kept in step with
    /// the scene: rebuilt before the next simulation step after instances change, and right after
    /// loading a world. Body user data: the vob id (0 for terrain and ground plate).
    [[nodiscard]] const physics::PhysicsWorld& physics() const noexcept { return m_physics; }
    /// The player's character, or nullptr without a player (no start point, editor, benchmark).
    [[nodiscard]] const physics::CharacterController* player() const noexcept
    {
        return m_player.valid() ? &m_player : nullptr;
    }
    /// True while the player drives the camera; false in the free debug camera (debug_fly, F3).
    [[nodiscard]] bool playerCameraActive() const noexcept { return m_player.valid() && !m_flyMode; }
    /// Fly mode (debug_fly, F3, --fly): the free camera with mouse look; with a player it waits meanwhile.
    [[nodiscard]] bool flyMode() const noexcept { return m_flyMode; }
    void setFlyMode(bool on);
    /// The current view as start options (what copy_position, F6, copies).
    [[nodiscard]] std::string viewLine() const;
    [[nodiscard]] const gameplay::PlayerMovement& playerMovement() const noexcept { return m_movement; }
    [[nodiscard]] const gameplay::MovementSettings& movementSettings() const noexcept
    {
        return m_movementSettings;
    }
    /// Turns the player to `yaw` (radians, 0 = -Z) at once (autopilot); movement keeps its speed.
    void steerPlayer(f32 yaw);
    /// The last landing of the player (nullopt before the first).
    [[nodiscard]] const std::optional<PlayerLanding>& lastLanding() const noexcept { return m_lastLanding; }
    /// Saves the next rendered frame as PNG (no effect without rendering).
    void requestScreenshot(fs::Path file) { m_screenshotRequest = std::move(file); }
    /// Puts the player's feet at `feet` facing `yaw` (radians, 0 = -Z), motion stopped; debugging, tests.
    void teleportPlayer(const Vec3& feet, f32 yaw);
    /// State of the hero's animation state machine ("move", "jump" ...); empty without an animated figure.
    [[nodiscard]] std::string_view playerAnimationState() const noexcept;
    /// VFS path of the animated hero figure; empty without one.
    [[nodiscard]] std::string_view playerFigurePath() const noexcept;
    /// Hangs the model at `modelPath` (VFS) at a socket bone of the hero ("socket_hand_r" ...), replacing
    /// what the socket held; drawn with the figure, shadows included. Errors: no figure, no such socket or
    /// model.
    [[nodiscard]] Result<void> attachToPlayer(std::string_view socket, std::string_view modelPath);
    /// The same for a model made in code (tests, debug UI); `name` for the log.
    [[nodiscard]] Result<void> attachToPlayer(std::string_view socket, const asset::MeshData& mesh,
                                              std::string_view name);
    void detachFromPlayer(std::string_view socket);
    /// World transform of a socket in the pose of the last fixed step (nullopt: no figure or socket).
    [[nodiscard]] std::optional<Mat4> playerSocketTransform(std::string_view socket) const;
    /// The hero's head turns towards this world point (within limits); nullopt: straight ahead.
    void setPlayerLookTarget(std::optional<Vec3> target);
    /// Face: "angry", "friendly", "fear", "pain", "sleep" or "" (neutral); false for an unknown name.
    bool setPlayerExpression(std::string_view name, f32 weight = 1.0f);
    void setPlayerTalking(bool talking);
    /// Face morph weights of the hero (animation::FaceMorph order); empty without a figure.
    [[nodiscard]] std::span<const f32> playerFaceWeights() const noexcept;
    /// Current head turn of the hero in degrees (positive: towards the figure's left).
    [[nodiscard]] f32 playerLookYawDegrees() const noexcept;
    /// Heroes assembled from parts ([game] hero = "...figure.toml"): swaps a part ("body", "head", "hair",
    /// "beard"; paths relative to characters/, empty removes hair or beard) or all garments ("cloth" pieces,
    /// armour, headgear) and rebuilds the figure; the animation goes on. Errors leave the figure as it was.
    [[nodiscard]] Result<void> setPlayerPart(std::string_view role, std::string_view partPath);
    [[nodiscard]] Result<void> setPlayerCloth(std::span<const std::string> partPaths);
    /// The parts the hero wears (nullopt: no figure, or an assembled .glb).
    [[nodiscard]] std::optional<asset::FigureManifest> playerFigureManifest() const;
    /// Triangles of the hero figure drawn (LOD 0; 0 without rendering).
    [[nodiscard]] usize playerFigureTriangles() const noexcept;

    /// Animals for tests and the debug UI until M9 brings monsters (AI, collision, vobs): "wolf", "keiler",
    /// "laufvogel" at `feet` facing `yaw` (radians, 0 = -Z, positive left); moved by their clips' root
    /// motion, on the ground. Gone with the world. Returns the creature's id.
    [[nodiscard]] Result<u32> spawnCreature(std::string_view species, const Vec3& feet, f32 yaw);
    void removeCreatures();
    /// speed: m/s ahead (blend stand, walk, run); turn: -1 left, 1 right (turns on the spot while held).
    void setCreatureMove(u32 id, f32 speed, f32 turn = 0.0f);
    /// "attack_1", "attack_2", "hit", "threaten" (once), "eat", "sleep", "stop", "die", "revive"; false:
    /// unknown.
    bool creatureAction(u32 id, std::string_view action);
    /// Every action in turn (walk, run, turns, attacks, threaten, hit, eat, sleep, die, revive), for checking
    /// clips.
    void setCreatureShowcase(u32 id, bool on);
    [[nodiscard]] usize creatureCount() const noexcept;
    [[nodiscard]] std::string_view creatureState(u32 id) const noexcept;
    [[nodiscard]] std::optional<Vec3> creaturePosition(u32 id) const;
    [[nodiscard]] f32 creatureYaw(u32 id) const noexcept;

    /// Scripts (M7): runs a console line (Lua; an expression shows its value) and adds it with its result to
    /// the console; also for tests and tools.
    Result<script::Value> runConsoleLine(std::string_view line);
    [[nodiscard]] const std::vector<std::string>& consoleLines() const noexcept;
    void setConsoleOpen(bool open) noexcept;
    [[nodiscard]] bool consoleOpen() const noexcept { return m_consoleOpen; }
    /// The script VM of the session (nullptr before init).
    [[nodiscard]] const script::ScriptVm* scripts() const noexcept;
    /// docs/script-api.md: every engine function and event scripts can use.
    [[nodiscard]] std::string scriptApiMarkdown() const;
    /// Loads the changed scripts again (also done by itself in development builds); the story variables stay.
    void reloadScripts();
    /// Items placed by `insert` since the start (tests).
    [[nodiscard]] u32 insertedItemCount() const noexcept { return m_insertedItems; }
    /// True while the player climbs a ledge (input is ignored until it stands on top).
    [[nodiscard]] bool playerClimbing() const noexcept { return m_climb.has_value(); }
    /// Swimming or diving (gameplay::WaterMode::Land on land), and the air left under water.
    [[nodiscard]] gameplay::WaterMode playerWaterMode() const noexcept { return m_swimmer.mode(); }
    [[nodiscard]] f32 playerAirSeconds() const noexcept { return m_swimmer.airSeconds(); }
    /// Hit points lost by drowning since the player was put into the world (hit points: M8).
    [[nodiscard]] f32 drownDamage() const noexcept { return m_drownDamage; }
    /// The water vobs of the loaded world.
    [[nodiscard]] const world::WaterBodies& water() const noexcept { return m_water; }
    /// Hit points the last fall cost (0: none yet or a harmless one); hit points themselves come with M8.
    [[nodiscard]] f32 lastFallDamage() const noexcept { return m_lastFallDamage; }
    /// Tests and demos: replaces the keyboard/mouse input of the player (nullopt: back to the actions).
    /// A jump in it counts once, when it turns on.
    void setPlayerInputOverride(std::optional<gameplay::MoveInput> input) { m_playerInputOverride = input; }

private:
    void shutdown();
    [[nodiscard]] Result<void> initShaders();
    [[nodiscard]] Result<void> initSceneRendering();
    [[nodiscard]] Result<void> initViewMesh();
    [[nodiscard]] Result<void> initScene();
    [[nodiscard]] Result<void> initWorld();
    /// Builds the world from `file` (read from `path` or kept from an earlier visit) and puts the camera on
    /// the start point `start` (empty: the lowest id or the overview).
    [[nodiscard]] Result<void> loadWorld(const std::string& path, world::WorldFile file,
                                         std::string_view start);
    void unloadWorld();
    void performWorldChange();
    /// Rebuilds m_physics from the terrain and m_instances if they changed (EnginePhysics.cpp).
    void syncPhysics();
    // Player (EnginePlayer.cpp)
    void initPlayer();
    void spawnPlayer();
    void removePlayer();
    void refreshMovementSettings();
    void updatePlayerInput(bool allowMouse, bool allowKeyboard);
    void fixedUpdatePlayer(f32 seconds);
    void updatePlayerCamera(f64 realSeconds);
    void drawPlayer(bool shadow, u32 cascade);
    void movePlayer(f32 seconds, const gameplay::MoveInput& input);
    void startClimb(const Vec3& from, const Vec3& to, gameplay::LedgeClass ledge);
    // Hero figure (EngineFigure.cpp)
    [[nodiscard]] Result<std::unique_ptr<PlayerFigure>> loadFigure(std::string_view path,
                                                                   std::string_view graphPath);
    void loadPlayerFigure();
    void resetPlayerAnimation();
    void animatePlayer(f32 seconds, const gameplay::MoveInput& input);
    [[nodiscard]] bool drawPlayerFigure(const Mat4& transform, bool shadow, u32 cascade);
    void drawPlayerSockets();
    void playerAnimationUi();
    [[nodiscard]] Mat4 playerFigureTransform() const;
    void preparePlayerPose(f32 alpha);
    [[nodiscard]] Result<asset::SkinnedModelData> assembleFigureParts(const asset::FigureManifest& manifest);
    [[nodiscard]] Result<void> uploadFigure(AnimatedFigure& figure, const asset::SkinnedModelData& data);
    using FigureGraphCallback =
        std::function<void(const animation::AnimGraph&, std::span<const asset::AnimationSetData* const>)>;
    /// Model (.glb or *.figure.toml), graph, clips, skeleton, animator, GPU side; `extra` sees the graph and
    /// sets.
    [[nodiscard]] Result<void> loadAnimatedFigure(AnimatedFigure& figure, std::string_view path,
                                                  std::string_view graphPath,
                                                  const FigureGraphCallback& extra = {});
    void prepareFigurePose(AnimatedFigure& figure, f32 alpha);
    void drawAnimatedFigure(AnimatedFigure& figure, const Mat4& transform, bool shadow, u32 cascade);
    // Animals (EngineCreatures.cpp)
    void fixedUpdateCreatures(f32 seconds);
    void drawCreatures(bool shadow, u32 cascade);
    [[nodiscard]] Result<u32> spawnAnimated(std::string_view label, std::string_view model,
                                            std::string_view graph, const Vec3& feet, f32 yaw);
    // Scripts (EngineScripts.cpp)
    void mountScripts();
    [[nodiscard]] Result<void> initScripts();
    void bindEngineFunctions();
    void updateScripts(f64 realSeconds);
    [[nodiscard]] std::string scriptStamp() const;
    [[nodiscard]] Result<void> insertInstance(std::string_view name, u32 count);
    void consolePrint(std::string line);
    void consoleUi();
    void creaturesUi();
    [[nodiscard]] Creature* creature(u32 id) noexcept;
    [[nodiscard]] const Creature* creature(u32 id) const noexcept;
    [[nodiscard]] Result<void> rebuildPlayerFigure(const asset::FigureManifest& manifest);
    void refreshOutfitChoices();
    [[nodiscard]] Result<void> attachModel(std::string_view socket, const LoadedModel* model,
                                           std::unique_ptr<LoadedModel> owned);
    [[nodiscard]] f32 climbDuration(gameplay::LedgeClass ledge) const;
    [[nodiscard]] Vec3 climbPosition() const;
    void drawPlayerDebug();
    [[nodiscard]] Vec3 triggerProbePosition() const;
    /// Models no instance uses any more (after a level change) go, with their geometry and textures.
    void releaseUnusedModels();
    /// Render instances and lights for the vobs in m_scene (mesh and light vobs).
    [[nodiscard]] Result<void> instantiateScene();
    /// Splat layers and holes of the loaded terrain; layers that fail to load are a warning (slope
    /// colours instead), holes always apply.
    void loadTerrainSurface(const world::TerrainRef& ref);
    /// Puts the camera on the start point (--start or the lowest id); no start point and no --start
    /// keeps the overview camera, an unknown name is an error.
    [[nodiscard]] Result<void> applyStartPoint(std::string_view name);
    void addWorldDebugOverlay();
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
    void addInstance(const LoadedModel& model, const Mat4& transform, bool sizeCullable = true,
                     world::VobId vob = {});
    void setViewpoint(const SceneViewpoint& viewpoint);
    void updateBenchmark(f64 realSeconds);
    void saveScreenshot(u32 width, u32 height, const fs::Path& file);
    void initEnvironment();
    /// Light, fog and sky from the day cycle at the current game time.
    void updateEnvironment();
    void renderScene(u32 width, u32 height);
    void drawScene(u32 width, u32 height);
    void addDebugOverlay(u32 width, u32 height);
    /// `allowMouse` / `allowKeyboard` false while the debug UI uses them.
    void updateDebugCamera(f64 realSeconds, bool allowMouse, bool allowKeyboard);
    // Fly mode, start view, notices (EngineView.cpp)
    void applyStartView();
    void copyViewToClipboard();
    void showNotice(std::string text, f64 seconds);
    [[nodiscard]] bool noticeVisible() const noexcept;
    void drawNotice(u32 height);
    void runDebugUi(f64 realSeconds);

    EngineConfig m_config;
    // Assets: the manager is destroyed before the VFS it reads from (member order).
    asset::Vfs m_vfs;
    std::unique_ptr<asset::AssetManager> m_assets;
    std::unique_ptr<platform::Window> m_window;
    std::unique_ptr<platform::GlContext> m_glContext; // must outlive m_device
    std::unique_ptr<render::Device> m_device;
    /// Shared vertex/index storage of all models (outlives them: reset after the models).
    std::unique_ptr<render::GeometryArena> m_geometry;
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
    world::Scene m_scene;       // world vobs (--world, --scene); render instances are built from it
    world::GameTime m_gameTime; // global: runs across level changes (save game: save.md)
    world::DayCycle m_dayCycle = world::DayCycle::fallback();
    render::Sky m_sky;
    f32 m_fogBaseDensity = 0.0f;     // [render] fog_density; the curves scale it
    bool m_sunEnabled = true;        // debug: sun and moon light off
    world::TriggerSystem m_triggers; // fed with the camera until the player exists (M5)
    std::string m_worldPath;
    world::WorldFile m_worldFile; // the loaded world without its vobs (terrain, waynet ... for capturing)
    struct LeftWorld
    {
        world::WorldFile file;       // as it was when left
        std::set<u64> spentTriggers; // once-triggers that fired there
    };
    std::map<std::string, LeftWorld> m_leftWorlds; // by lower-case path; into the save game later
    struct PendingWorldChange
    {
        std::string world;
        std::string start;
    };
    std::optional<PendingWorldChange> m_pendingWorldChange;
    std::vector<EngineTool*> m_tools;
    world::Heightfield m_heightfield;  // terrain heights (empty without terrain)
    render::TerrainRenderer m_terrain; // pipelines reference ShaderLibrary programs
    bool m_hasTerrain = false;
    std::map<std::string, std::unique_ptr<LoadedModel>, std::less<>> m_models; // by VFS path
    std::unique_ptr<LoadedModel> m_groundModel;
    std::unique_ptr<LoadedModel> m_waterModel; // translucent placeholder surface of water vobs (until M17)
    world::WaterBodies m_water;
    std::vector<SceneInstance> m_instances;
    std::string m_sceneName;
    AABB m_sceneBounds{Vec3(1.0f), Vec3(-1.0f)}; // empty until the first non-ground instance
    std::vector<SceneViewpoint> m_viewpoints;
    u32 m_visibleInstances = 0;
    u32 m_culledFar = 0;
    u32 m_culledSmall = 0;
    render::CullSettings m_cullSettings;
    render::CullGrid m_cullGrid; // over m_instances; rebuilt when instances change
    bool m_cullGridDirty = true;
    physics::PhysicsWorld m_physics;
    // Player (M5): character, movement and camera. Drawn feet at the last two fixed steps for interpolation.
    physics::CharacterController m_player;
    gameplay::PlayerMovement m_movement;
    gameplay::ThirdPersonCamera m_playerCamera;
    gameplay::MovementSettings m_movementSettings;
    asset::Handle<gameplay::MovementSettings> m_movementData; // data/movement.toml, hot reload
    u32 m_movementVersion = 0;
    gameplay::MoveInput m_playerInput; // keys of this frame + mouse gathered since the last step
    f32 m_playerPitchPixels = 0.0f;    // mouse up/down since the last camera update
    std::optional<gameplay::MoveInput> m_playerInputOverride;
    bool m_overrideJumped = false;              // jump of the override already used (edge)
    std::optional<gameplay::ClimbPath> m_climb; // climbing a ledge
    f32 m_climbSeconds = 0.0f;
    f32 m_jumpCooldown = 0.0f; // s until the next jump (after landing)
    f32 m_lastFallDamage = 0.0f;
    std::optional<PlayerLanding> m_lastLanding;
    std::optional<fs::Path> m_screenshotRequest;
    gameplay::Swimmer m_swimmer;
    f32 m_drownDamage = 0.0f;
    f32 m_drownLogged = 0.0f; // damage already reported in the log (whole points)
    Vec3 m_playerFeetBefore{0.0f};
    Vec3 m_playerFeet{0.0f};
    f32 m_playerYawBefore = 0.0f;
    bool m_flyMode = false;                             // debug_fly: free camera while the player waits
    bool m_playerMouse = false;                         // relative mouse captured for the player camera
    const LoadedModel* m_playerModel = nullptr;         // static placeholder when no animated figure loads
    std::unique_ptr<PlayerFigure> m_figure;             // animated hero (M6)
    std::vector<std::unique_ptr<Creature>> m_creatures; // animals (M6 D3, until M9)
    u32 m_nextCreatureId = 1;
    std::string m_creatureSpecies = "wolf";                   // debug UI choice
    std::unique_ptr<script::ScriptVm> m_scripts;              // Lua of the game session (M7)
    std::vector<std::unique_ptr<LoadedModel>> m_scriptModels; // placeholders of inserted items
    u32 m_insertedItems = 0;
    std::string m_scriptStamp; // newest script changes seen (hot reload)
    f64 m_scriptReloadTimer = 0.0;
    bool m_consoleOpen = false;
    bool m_consoleFocus = false; // give the input the keyboard on the next console frame
    std::vector<std::string> m_consoleLines;
    std::vector<std::string> m_consoleHistory;
    f32 m_figureExpressionWeight = 1.0f; // debug UI slider
    bool m_physicsDirty = true;          // set together with m_cullGridDirty and on terrain changes
    std::vector<u32> m_cullCandidates;   // per pass, reused
    bool m_multiDraw = true;             // [render] multi_draw: batches instead of one draw per mesh
    std::vector<render::MeshDrawItem> m_drawItems; // per pass, reused
    FrameTimes m_benchmarkTimes;
    std::vector<FrameTimeSummary> m_benchmarkResults;
    std::vector<render::FrameStats> m_benchmarkStats; // per viewpoint, of its last measured frame
    std::vector<render::BatchStats> m_benchmarkBatches;
    std::vector<u32> m_benchmarkTerrainChunks;
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
    std::string m_notice; // short message at the bottom of the screen (fly mode keys, "position copied")
    f64 m_noticeUntil = 0.0;
    f64 m_realTime = 0.0; // seconds of frames since start (notices)
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

#pragma once

// Module: g7::runtime
// Verbindet alle Engine-Module zu einer lauffaehigen Anwendung: Initialisierungsreihenfolge,
// Hauptschleife (fester Simulationsschritt + interpoliertes Rendern), Shutdown.
// Spezifikation: docs/02-architecture.md ("Hauptschleife", "Initialisierung")

#include <g7/ai/Waynet.hpp>
#include <g7/asset/AssetManager.hpp>
#include <g7/asset/FigureAssembly.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/asset/VoiceLines.hpp>
#include <g7/core/Clock.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/gameplay/Character.hpp>
#include <g7/gameplay/Combat.hpp>
#include <g7/gameplay/Focus.hpp>
#include <g7/gameplay/Magic.hpp>
#include <g7/gameplay/Mobs.hpp>
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

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
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
    u32 lod = 0;              ///< level of detail drawn now (chosen by distance with hysteresis)
};

/// A landing of the player: fall height from the highest point and the hit points it cost.
/// An item lying in the world (M8), as the engine reports it.
struct WorldItemInfo
{
    world::VobId vob;
    std::string instance;
    u32 count = 1;
    Vec3 position{0.0f};
};

/// What the hero has in focus (M8): kind, id (VobId value for items and mobs, creature id for NPCs) and its
/// name.
struct FocusInfo
{
    gameplay::FocusKind kind = gameplay::FocusKind::Item;
    u64 id = 0;
    std::string name;
};

/// A mob vob as the engine reports it (M8 part C): its Lua definition, type, focus name, lock and contents.
struct MobInfo
{
    std::string definition;
    std::string type;
    std::string name;
    bool locked = false;
    bool open = false; ///< chest lid up, door open
    std::vector<gameplay::ItemStack> contents;
};

/// What the hero does at a mob (keys, or the lockpick window's buttons).
enum class MobCommand : u8
{
    Leave,    ///< stop using it (the leave clip plays)
    TurnLeft, ///< lockpicking
    TurnRight,
};

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
    /// Seeds the engine's random numbers and Lua's math.random (tests: the same run every time); unset:
    /// random.
    std::optional<u32> randomSeed;
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
    /// Console lines (Lua) run once after start (--exec, repeatable), e.g. `insert_npc('npc_farmer_woman')`.
    std::vector<std::string> exec;
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
    /// Level of detail for every static model (--lod=0|1|2, test images); -1 = by distance (--lod=auto).
    i32 forcedLod = -1;
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
    /// Shadow tiles drawn per cascade since start (far cascades less often than every frame).
    [[nodiscard]] const std::array<u32, 4>& cascadeDraws() const noexcept { return m_cascadeDraws; }
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
    /// The spell a rune or scroll casts (M12); nullopt for other items.
    [[nodiscard]] std::optional<gameplay::SpellInfo> spellOfItem(std::string_view item) const;
    /// The rooms (zones of type indoor) nearest to `point`, at most render::kMaxIndoorVolumes; the renderer
    /// gives them the indoor ambient (environment.toml [indoor]).
    [[nodiscard]] std::vector<render::IndoorVolume> nearestIndoorVolumes(const Vec3& point) const;
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
    /// The hero's character (M8): Npc "pc_hero" from the scripts; nullptr without it.
    [[nodiscard]] const gameplay::Character* hero() const noexcept;

    /// Levels of detail of static models (asset.md "Detailstufen"): thresholds, forcing (--lod, debug UI).
    [[nodiscard]] const render::LodSettings& lodSettings() const noexcept { return m_lodSettings; }
    void setForcedLod(i32 lod) noexcept { m_lodSettings.forced = lod < 0 ? -1 : std::min(lod, 2); }
    /// Instances drawn per level in the last frame's main pass.
    [[nodiscard]] const std::array<u32, 3>& lodCounts() const noexcept { return m_lodCounts; }

    // NPC navigation (M9 part A, EngineNpcs.cpp)
    /// The world's waynet (empty without a waynet block).
    [[nodiscard]] const ai::Waynet& waynet() const noexcept { return m_waynet; }
    /// Sends an NPC walking (or running) to a way point or freepoint by name, without regard to case; it
    /// plans a route over the waynet (straight where nothing is in the way). Errors: not an NPC, unknown
    /// target, no way.
    [[nodiscard]] Result<void> npcGoTo(u32 id, std::string_view target, bool run = false);
    /// The same to a position (the player ...); `label` names it in npc_arrived.
    [[nodiscard]] Result<void> npcGoToPosition(u32 id, const Vec3& goal, std::string_view label,
                                               bool run = false);

    // Perception (M9 part C, EnginePerception.cpp)
    /// A noise NPCs may hear: within `radius` (times their hearing) they get assess_noise(npc, kind, x, y,
    /// z).
    void emitNoise(const Vec3& at, f32 radius, std::string_view kind);
    /// The radius of a noise kind from the scripts (Perception.noise.<kind>), 0 if none.
    [[nodiscard]] f32 noiseRadius(std::string_view kind) const;
    /// Whether the NPC sees the player now (cone, range - shorter when sneaking or at night -, line of
    /// sight).
    [[nodiscard]] bool npcSeesPlayer(u32 id) const;
    /// The hero's drawn weapon: 0 none, 1 one-handed, 2 fists.
    [[nodiscard]] u8 weaponMode() const noexcept { return m_weaponMode; }
    /// Draws the equipped melee weapon (fists without one) or puts it away.
    void toggleWeapon();
    /// The figure's "draw"/"sheath" events: the weapon model appears in or leaves the hand.
    void weaponEvent(std::string_view event);
    /// Whether the hero's figure holds a model at this socket (sword in socket_hand_r ...).
    [[nodiscard]] bool playerHolds(std::string_view socket) const;
    [[nodiscard]] bool npcWalking(u32 id) const noexcept;
    /// The first inserted NPC of an Npc instance.
    [[nodiscard]] std::optional<u32> npcByInstance(std::string_view instance) const noexcept;
    // Dialogues (M10 part A, EngineDialog.cpp)
    /// Starts talking to an NPC: an important Info runs first, then the menu of Infos.
    [[nodiscard]] Result<void> startDialog(u32 npc);
    [[nodiscard]] bool inDialog() const noexcept { return m_dialog.has_value(); }
    /// The player camera: 0 outside .. 1 inside (indoor profile, [camera.indoor]); its distance behind the
    /// target.
    [[nodiscard]] f32 playerIndoorBlend() const noexcept { return m_indoorBlend; }
    /// 0 .. 1: the combat profile of the camera ([camera.combat], M11 K5).
    [[nodiscard]] f32 playerCombatBlend() const noexcept { return m_combatBlend; }
    [[nodiscard]] f32 playerCameraDistance() const noexcept { return m_playerCamera.distance(); }
    /// K5: the enemy the hero has locked while his weapon is drawn (its Npc instance).
    [[nodiscard]] std::optional<std::string> heroCombatTarget() const;
    /// K7: takes `count` (0: all) of `item` from a knocked out or dead NPC next to the hero; returns how
    /// many.
    Result<u32> loot(std::string_view npc, std::string_view item, u32 count);
    /// Picks the n-th entry (from 0) of the menu shown (an Info, an answer, "Ende").
    void dialogChoose(usize index);
    /// Skips the line being said.
    void dialogSkip();
    void endDialog();
    // Trading (M10 part C, EngineTrade.cpp)
    [[nodiscard]] bool trading() const noexcept { return m_trade.has_value(); }
    [[nodiscard]] Result<void> tradeBuy(std::string_view item, u32 count = 1);
    [[nodiscard]] Result<void> tradeSell(std::string_view item, u32 count = 1);
    void closeTrade();
    // Diary (M10 part D, EngineDiary.cpp)
    void setDiaryOpen(bool open) noexcept { m_diaryOpen = open; }
    [[nodiscard]] bool diaryOpen() const noexcept { return m_diaryOpen; }
    /// What the window "Tagebuch" shows (from Story).
    [[nodiscard]] ui::DiaryPanel diaryPanelData() const;
    /// Price of one piece: the hero buys (full value x Trade.sell_factor) or sells (x Trade.buy_factor).
    [[nodiscard]] i64 tradePrice(std::string_view item, bool heroBuys) const;
    /// Whether one can walk straight from a to b (a sphere at knee-to-hip height meets nothing).
    [[nodiscard]] bool walkableLine(const Vec3& a, const Vec3& b) const;

    // Items, focus, picking up (M8 part B, EngineItems.cpp)
    /// Items lying in the world (item vobs of the world file plus those inserted or dropped).
    [[nodiscard]] std::vector<WorldItemInfo> worldItems() const;
    /// Puts an item vob (runtime id) into the world; the Item instance must exist.
    [[nodiscard]] Result<world::VobId> spawnItem(std::string_view instance, u32 count, const Vec3& at,
                                                 f32 yaw = 0.0f);
    /// The focus of the last frame (hero only; none in fly mode).
    [[nodiscard]] std::optional<FocusInfo> focus() const;
    /// Recomputes the focus now (normally once per frame after the simulation).
    void updateFocus();
    /// What the action key does with an item in focus: starts picking it up (it reaches the inventory with
    /// the animation's "pickup" event, or after a moment without one). Errors: nothing to pick up, already
    /// busy.
    [[nodiscard]] Result<void> pickUpFocus();
    [[nodiscard]] bool pickingUp() const noexcept { return m_pickup.has_value(); }
    /// Puts `count` of the hero's items on the ground in front of him.
    [[nodiscard]] Result<void> dropItem(std::string_view instance, u32 count = 1);
    [[nodiscard]] bool inventoryOpen() const noexcept { return m_inventoryOpen; }
    void setInventoryOpen(bool open) noexcept;
    [[nodiscard]] const gameplay::FocusSettings& focusSettings() const noexcept { return m_focusSettings; }

    // Mobs (M8 part C, EngineMobs.cpp)
    /// The hero walks to the nearest slot of the mob and uses it: enter clip, loop (chest: its contents next
    /// to the inventory), leave clip. A locked mob opens with its key; with a lockpick the lockpicking
    /// starts. Errors: busy, no such mob, no slot, locked without key and lockpick ("locked").
    [[nodiscard]] Result<void> useMob(world::VobId vob);
    [[nodiscard]] Result<void> useFocusedMob();
    /// "approach", "picklock", "enter", "loop", "leave" while using a mob.
    [[nodiscard]] std::optional<std::string_view> mobPhase() const noexcept;
    void mobCommand(MobCommand command);
    [[nodiscard]] std::optional<MobInfo> mobInfo(world::VobId vob) const;
    /// The mob vob of this name ("LAGER_TRUHE").
    [[nodiscard]] std::optional<world::VobId> findMob(std::string_view vobName) const;
    [[nodiscard]] Result<void> takeFromMob(world::VobId vob, std::string_view item, u32 count = 1);
    [[nodiscard]] Result<void> putIntoMob(world::VobId vob, std::string_view item, u32 count = 1);
    /// Numbers in [0, 1) for chances (lockpicks breaking); tests set a fixed one.
    void setRandomSource(std::function<f32()> random) { m_random = std::move(random); }
    /// What the hero can choose at the mob now (anvil: recipes for which he has the material, bed: until when
    /// to sleep); empty when there is nothing to choose (M8 part C2).
    [[nodiscard]] std::vector<std::string> mobChoices() const;
    /// Picks option `index` of mobChoices(): the anvil forges the recipe (its strikes, then the items
    /// change), the bed sleeps until that hour (hit points and mana full) and the hero gets up.
    [[nodiscard]] Result<void> chooseMobOption(usize index);
    // Using items, pickpocketing (M8 part D, EngineUse.cpp)
    /// The hero uses an item: food and potions take effect at the "use" event of t_eat / t_drink and are used
    /// up, documents open (t_read_scroll). Only while standing (else "Nicht jetzt.").
    [[nodiscard]] Result<void> useItem(std::string_view item);
    [[nodiscard]] bool usingItem() const noexcept { return m_itemUse.has_value(); }
    struct Document
    {
        std::string title;
        std::string text;
    };
    /// The document being read (window "Document"); nullopt when none is open.
    [[nodiscard]] std::optional<Document> document() const;
    void closeDocument() noexcept;
    /// The action key while sneaking on an NPC in focus (Gothic 1): needs the talent pickpocket; with
    /// dexterity
    /// >= the NPC's pickpocket_dex it takes one item not worn, otherwise the NPC notices. Once per NPC.
    [[nodiscard]] Result<void> pickpocketFocus();
    [[nodiscard]] bool pickpocketing() const noexcept { return m_pickpocket.has_value(); }
    /// An inserted NPC's inventory (nullopt for animals).
    [[nodiscard]] std::optional<std::vector<gameplay::ItemStack>> creatureInventory(u32 id) const;
    /// The last short message to the player ("Verschlossen.", "Der Dietrich ist abgebrochen."; also fly mode
    /// hints).
    [[nodiscard]] const std::string& lastNotice() const noexcept { return m_notice; }
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
    /// Whether shadow cascade `i` is drawn this frame (m_cascades holds the fresh fit).
    [[nodiscard]] bool shadowRedraw(u32 i) const;
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
    // Hero character (EngineHero.cpp)
    void buildHero();
    // NPC navigation (EngineNpcs.cpp)
    [[nodiscard]] std::optional<Vec3> navigationTarget(std::string_view name) const;
    void walkNpc(Creature& c, f32 seconds);
    /// Unlocked door mob (NPCs plan through it and open it).
    [[nodiscard]] bool passableDoor(u64 vob) const;
    [[nodiscard]] std::optional<Vec3> doorLeafCentre(u64 vob) const;
    /// Opens a closed door ahead of the walking NPC, closes the one behind it; true while it waits for one.
    bool npcDoors(Creature& c);
    /// Spawns an Npc instance with its figure, capsule, values and routine.
    [[nodiscard]] Result<u32> spawnNpc(std::string_view name, const Vec3& at, f32 yaw);
    // Behaviour (EngineAi.cpp)
    void fixedUpdateAi(Creature& c, f32 seconds);
    void perceive(Creature& c, f32 seconds);
    [[nodiscard]] bool seesPlayer(const Creature& c) const;
    /// The player did something (theft, used a mob) that NPCs who see it react to: `event`(npc, args...).
    void witnessed(std::string_view event, std::span<const script::Value> arguments,
                   std::string_view alsoNpc = {});
    /// The player entered a private area of `owner` (Npc instance or guild).
    void enteredPrivateArea(std::string_view owner, std::string_view area);
    [[nodiscard]] bool ownedBy(const Creature& c, std::string_view owner) const;
    void loadPerceptionSettings();
    void bindPerceptionFunctions();
    // Dialogues (EngineDialog.cpp)
    [[nodiscard]] std::vector<const script::Instance*> availableInfos(std::string_view npc, bool important);
    void runInfo(const script::Instance& info);
    void buildDialogMenu();
    void queueLine(std::string speaker, std::string text);
    void fixedUpdateDialog(f32 seconds);
    void dialogInput();
    void dialogUi();
    void dialogPerception(Creature& c, f32 distance, bool sees);
    void presentLine(const struct DialogLineRef& line);
    void loadDialogPresentation();
    void loadTradeSettings();
    [[nodiscard]] Result<void> openTrade();
    [[nodiscard]] i64 itemValue(std::string_view item) const;
    [[nodiscard]] gameplay::Character* trader();
    void tradeUi();
    void bindTradeFunctions();
    void diaryUi();
    void bindDiaryFunctions();
    void stopTalking();
    /// Over the listener's shoulder at the speaker (M10 part B); in updatePlayerCamera.
    void updateDialogCamera(f32 seconds);
    void bindDialogFunctions();
    void updateRoutine(Creature& c);
    void beginState(Creature& c, std::string_view state, std::string_view at);
    void finishState(Creature& c);
    void runCommands(Creature& c, f32 seconds);
    bool startCommand(Creature& c);
    void releaseFreepoint(Creature& c);
    void npcSays(const Creature& c, std::string_view text); ///< log, npc_said, shown near the player
    /// "@player" (or empty): the player's feet; otherwise the position of the NPC of that instance.
    [[nodiscard]] std::optional<Vec3> targetPosition(std::string_view target) const;
    /// What the window "AI" shows (M9 part E): every NPC's state, queue, perception.
    [[nodiscard]] ui::AiPanel aiPanelData();
    /// Capsule of an animal species (data/creatures.toml), the human one otherwise.
    [[nodiscard]] physics::CharacterDesc creatureBody(std::string_view species);
    /// A figure of `set` (data/figure_sets.toml, figuren) for the NPC, stable by its name; empty if none.
    [[nodiscard]] std::string figureFromSet(std::string_view set, std::string_view npc);
    /// item_to_hand / item_from_hand of an NPC's animation: its hand item appears or goes.
    void handEvent(Creature& c, std::string_view event);
    [[nodiscard]] Creature* npcNamed(std::string_view instance) noexcept;
    void bindAiFunctions();
    void drawWaynet();
    void bindNpcFunctions();
    // Items, focus, picking up (EngineItems.cpp)
    struct WorldItem
    {
        world::VobId vob;
        const LoadedModel* model = nullptr;
        Mat4 transform{1.0f};
        AABB bounds;
    };
    struct FocusTarget
    {
        u64 id = 0;
        gameplay::FocusKind kind = gameplay::FocusKind::Item;
        std::string name;
        Vec3 point{0.0f};
    };
    struct PendingPickup
    {
        world::VobId vob;
        f32 time = 0.0f;
        bool animated = false; ///< the graph plays "pickup"
        bool taken = false;
    };
    void loadFocusSettings();
    // Mobs (EngineMobs.cpp)
    struct MobRuntime
    {
        std::string definition; ///< the Lua Mob instance (or a bare type)
        std::string type;
        std::string name;
        std::string lock;  ///< combination, empty: none
        std::string key;   ///< item that opens it
        std::string owner; ///< Npc or guild (taking from it is theft)
        bool locked = false;
        bool open = false;
        std::map<std::string, u32, std::less<>> contents;
        Quat closedRotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< local rotation as loaded (doors turn from here)
        f32 doorAngle = 0.0f;
        f32 doorFrom = 0.0f;
        f32 doorTo = 0.0f;
        f32 doorTime = -1.0f; ///< 0..1 while swinging, -1: still
    };
    struct MobUse
    {
        enum class Phase : u8
        {
            Approach,
            Picklock,
            Enter,
            Loop,
            Leave,
        };
        world::VobId vob;
        std::string type;
        gameplay::SlotPlace place;
        Phase phase = Phase::Approach;
        f32 time = 0.0f;
        Vec3 fromFeet{0.0f};
        f32 fromYaw = 0.0f;
        f32 approachSeconds = 0.1f;
        bool picklock = false;
        std::optional<gameplay::Lockpick> lockpick;
        std::string lockpickResult;
        bool leaveRequested = false;
        bool eventFired = false;
        bool animated = false; ///< the graph plays the phase's state
        std::string state;     ///< "chest_enter" ...
        bool containerOpen = false;
        std::string recipe;  ///< anvil: the Recipe being forged
        u32 strikesLeft = 0; ///< anvil: hammer blows still to come
        f32 strikeTimer = 0.0f;
        std::string choiceMessage;
    };
    struct MobBody
    {
        physics::BodyId body;
        physics::ShapeId shape;
    };
    void loadMobTypes();
    // Magic (M12, EngineMagic.cpp).
    void bindMagicFunctions();
    // Combat (M11, EngineCombat.cpp).
    struct Combatant;
    void loadCombat();
    void bindCombatFunctions();
    void bindLootFunctions();
    void fixedUpdateCombat(f32 seconds);
    [[nodiscard]] std::optional<Combatant> combatant(u32 id);
    [[nodiscard]] std::string
    meleeWeapon(const Combatant& c) const; ///< drawn/equipped melee item, empty: fists
    [[nodiscard]] std::string fightMode(const Combatant& c) const; ///< "fist", "1h", "2h"
    [[nodiscard]] f32 reachOf(const Combatant& c) const;           ///< m beyond the bodies
    bool startFight(Combatant& c, std::string_view move, gameplay::AttackKind kind);
    void resolveHit(Combatant& attacker, Combatant& target);
    void stopForFight(Creature& c);
    void readCombatInput(); // K1: Gothic 1 keys, the mouse as second assignment
    void fixedUpdateHeroFight(gameplay::MoveInput& input, f32 seconds); // moves, lock (K5)
    [[nodiscard]] std::optional<u32> pickCombatTarget() const;
    [[nodiscard]] bool focusedNpcLying() const;
    // Ranged (M11 part E, EngineRanged.cpp).
    struct Projectile
    {
        Vec3 position{0.0f};
        Vec3 velocity{0.0f};
        std::string ammo;
        u32 shooter = 0; ///< creature id, ~0: the hero
        gameplay::DamageByType damage;
        f32 seconds = 0.0f;
    };
    [[nodiscard]] std::string rangedWeapon() const; ///< the hero's equipped bow or crossbow
    [[nodiscard]] bool rangedIsCrossbow(std::string_view item) const;
    void toggleRanged();
    [[nodiscard]] std::optional<Vec3> aimPoint(u32 creatureId) const;
    Result<void> shootRanged();
    void fixedUpdateProjectiles(f32 seconds);
    void projectileHit(const Projectile& p, u32 targetId);
    void drawProjectiles();
    void bindRangedFunctions();
    Result<void> lootFocus(); // opens the inventory with the lying NPC's belongings
    void loadVoiceLines();    // voice/lines.<language>.json: the keys of the spoken lines
    /// Key of a spoken line: dialogue "<info>_NN", else the NPC's shout "svm_<voice>_<m|f>_<occasion>_NN";
    /// empty if the voice database does not know the text.
    [[nodiscard]] std::string voiceKey(std::string_view info, std::string_view npc,
                                       std::string_view text) const;
    void rebuildMobs();
    void notice(std::string text);
    [[nodiscard]] std::string lockpickItem() const;
    [[nodiscard]] f32 lockpickBreakChance() const;
    bool playMobState(std::string_view state);
    [[nodiscard]] bool mobClipDone(const MobUse& use) const;
    void startMobPhase(MobUse& use, MobUse::Phase phase);
    void finishMobUse();
    void approachMob(f32 seconds);
    void fixedUpdateMobs(f32 seconds);
    void swingDoors(f32 seconds);
    void lockpickUi();
    // Using items, pickpocketing (EngineUse.cpp)
    struct ItemUse
    {
        std::string item;
        std::string category;
        std::string state; ///< "use_eat" ...
        f32 time = 0.0f;
        bool animated = false;
        bool applied = false;
    };
    struct PendingPickpocket
    {
        u32 creature = 0;
        f32 time = 0.0f;
        bool animated = false;
    };
    [[nodiscard]] bool heroStanding() const;
    void applyItemUse(ItemUse& use);
    void fixedUpdateItemUse(f32 seconds);
    void finishPickpocket(const PendingPickpocket& p);
    void documentUi();
    void bindUseFunctions();
    void choiceUi();
    [[nodiscard]] std::vector<const script::Instance*> recipesFor(std::string_view type) const;
    void finishRecipe(MobUse& use);
    void bindMobFunctions();
    void mobInput();
    [[nodiscard]] const LoadedModel* itemModel(std::string_view instance);
    void rebuildWorldItems();
    void removeWorldItem(world::VobId id);
    void appendItemDraws(const Frustum& volume, bool shadow, u32 cascade);
    [[nodiscard]] std::string focusName(gameplay::FocusKind kind, u64 id) const;
    void takeItem(world::VobId id);
    void fixedUpdateInteraction(f32 seconds);
    void inventoryUi();
    void focusUi();
    void bindHeroFunctions();
    [[nodiscard]] gameplay::ItemLookup itemLookup() const;
    [[nodiscard]] i32 xpForLevel(i32 level);
    void creaturesUi();
    void aiUi();         // window "AI" (M9 part E)
    void drawAiSenses(); // overlay: the selected NPC's sight cone and hearing
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
    render::LodSettings m_lodSettings;
    std::array<u32, 3> m_lodCounts{}; // instances drawn per level of detail in the last main pass
    render::CullGrid m_cullGrid;      // over m_instances; rebuilt when instances change
    bool m_cullGridDirty = true;
    physics::PhysicsWorld m_physics;
    // Player (M5): character, movement and camera. Drawn feet at the last two fixed steps for interpolation.
    physics::CharacterController m_player;
    gameplay::PlayerMovement m_movement;
    gameplay::ThirdPersonCamera m_playerCamera;
    f32 m_indoorBlend = 0.0f;
    f32 m_combatBlend = 0.0f; // 0 .. 1: weapon drawn and an enemy locked (M11, K5) // 0 outside .. 1 inside
                              // (a roof above the hero): the camera's indoor profile
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
    std::unique_ptr<gameplay::Character> m_hero;              // M8: kept over script reloads
    u32 m_insertedItems = 0;
    ai::Waynet m_waynet;               // of the loaded world (M9)
    std::vector<u32> m_freepointUsers; // creature id per freepoint, 0: free (M9 part B)
    // Perception (M9 part C)
    struct Noise
    {
        Vec3 at{0.0f};
        f32 radius = 0.0f;
        std::string kind;
        f64 time = 0.0;
    };
    std::vector<Noise> m_noises; // of the last two seconds
    struct PerceptionSettings
    {
        f32 sight = 25.0f;
        f32 angle = 100.0f;
        f32 sneakFactor = 0.5f;
        f32 nightFactor = 0.6f;
        f32 nearDistance = 20.0f; ///< below: 5 Hz, above: 1 Hz
        f32 forgetSeconds = 10.0f;
        f32 roomDistance = 8.0f; ///< an owner this near notices the player in the room without seeing him
        std::vector<std::pair<std::string, f32>> noises; ///< kind -> radius
    } m_perception;
    u8 m_weaponMode = 0;
    std::string m_weaponDrawn; // Item instance of the melee weapon being drawn
    bool m_drawWeaponRequested = false;
    f32 m_runNoiseTimer = 0.0f;
    u64 m_routineMinute = ~0ull;      // the game minute routines were last checked
    f32 m_simulationDistance = 80.0f; // [ai] simulation_distance: AI LOD
    // Dialogue (M10 part A)
    struct DialogLine
    {
        std::string speaker; ///< NPC instance or "hero"
        std::string name;    ///< shown: "Torwache", "Held"
        std::string text;
        std::string key; ///< "<info>_<nn>": translation and voice files later
        f32 seconds = 1.5f;
    };
    struct DialogOption
    {
        std::string text;
        std::string info;           ///< an Info of the menu
        script::FunctionRef choice; ///< an answer of the running Info
        bool end = false;           ///< "Ende"
    };
    struct Dialog
    {
        u32 npc = 0;
        std::string npcName;
        std::string info; ///< running
        u32 lineNumber = 0;
        std::deque<DialogLine> lines;
        f32 lineTime = 0.0f;
        bool linePresented = false;        ///< the front line's gesture and mouth started
        std::vector<DialogOption> choices; ///< answers added by the running Info
        std::vector<DialogOption> menu;    ///< shown when no line is said
        i32 selected = 0;
        bool endRequested = false;
        bool tradeRequested = false; ///< after the lines: the trade screen
    };
    std::optional<Dialog> m_dialog;
    struct Trade
    {
        u32 npc = 0;
    };
    std::optional<Trade> m_trade;
    bool m_diaryOpen = false; // window "Tagebuch" (action log)
    struct TradeSettings
    {
        std::string currency = "it_gulden"; ///< owner decision E6
        f32 sellFactor = 1.0f;              ///< the trader sells at the full value
        f32 buyFactor = 0.5f;               ///< and buys at half of it
    } m_tradeSettings;
    struct DialogPresentation
    {
        f32 side = 0.55f;
        f32 height = 1.62f;
        f32 back = 0.9f;
        f32 look = 1.55f;
        f32 blend = 0.25f;
        std::vector<std::string> gestures;
        std::vector<std::string> headGestures;
        std::vector<std::string> keep;
    } m_dialogPresentation;
    bool m_dialogCamera = false; // the camera follows the dialogue (blended from where it was)
    usize m_presentedLines = 0;  // lines of this dialogue presented (gesture, mouth)
    std::string m_aiFilter;      // window "AI"
    u32 m_aiSelected = 0;
    std::optional<Config> m_creatureBodies; // data/creatures.toml, read on first use (M9 part D)
    std::optional<Config> m_figureSets;     // data/figure_sets.toml (figuren), read on first use
    std::vector<WorldItem> m_worldItems;
    std::unordered_map<std::string, const LoadedModel*> m_itemModels; // by Item instance; models in m_models
                                                                      // or m_scriptModels (placeholders)
    std::unordered_map<std::string, Mat4> m_itemRest; // by Item instance: how its model lies on the ground
    gameplay::FocusSettings m_focusSettings;
    std::vector<gameplay::FocusCandidate> m_focusCandidates; // per frame, reused
    std::optional<FocusTarget> m_focus;
    std::optional<PendingPickup> m_pickup;
    bool m_pickupEvent = false; // the figure's "pickup" event fired
    bool m_inventoryOpen = false;
    std::string m_inventoryMessage;
    gameplay::MobTypes m_mobTypes;
    gameplay::CombatSettings m_combat; // data/combat.lua (M11)
    gameplay::Fighter m_heroFighter;
    std::vector<u32> m_heroHitThisSwing;
    struct CombatRequest
    {
        std::string move; ///< "attack", "parry", "dodge"
        gameplay::AttackKind kind = gameplay::AttackKind::Front;
    };
    std::optional<CombatRequest> m_combatRequest; // read per frame, used by the next fixed step
    std::optional<u32> m_combatTarget;            // K5: the locked enemy while the weapon is drawn
    std::optional<u32> m_lootTarget;              // K7: whose belongings the inventory window shows
    std::vector<Projectile> m_projectiles;        // arrows and bolts in flight (M11 part E)
    f32 m_rangedReload = 0.0f;                    // R2: seconds until the next shot
    bool m_drawRangedRequested = false;
    asset::VoiceLines m_voiceLines;
    std::unordered_map<u64, MobRuntime> m_mobs;    // by vob id
    void lockpickNoticed(const MobRuntime& m);     // witnesses of picking a lock (M9 part C)
    void storeDoors(world::WorldFile& file) const; // doors closed in the file, components.mob.open
    std::optional<MobUse> m_mobUse;
    std::vector<std::string> m_mobEvents;         // "open"/"close" of the hero's figure this step
    std::unordered_map<u64, MobBody> m_mobBodies; // collision of mob vobs (doors turn theirs)
    std::optional<ItemUse> m_itemUse;
    std::optional<Document> m_document;
    std::optional<PendingPickpocket> m_pickpocket;
    std::mt19937 m_rng{std::random_device{}()};
    std::function<f32()> m_random;
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
    std::vector<std::array<u32, 3>> m_benchmarkLods; // instances per level of detail, per viewpoint
    u64 m_benchmarkFrame = 0;
    render::MeshRenderer m_meshRenderer; // pipelines reference ShaderLibrary programs
    render::Environment m_environment;
    render::LightList m_lights;
    render::ShadowMap m_shadowMap;
    std::vector<render::Cascade> m_cascades;
    /// The cascades as last drawn into the shadow map: far ones are redrawn every 2nd/4th frame
    /// (shadowRedraw).
    std::vector<render::Cascade> m_cascadesDrawn;
    bool m_shadowCadence = true;         ///< [render] shadow_far_cadence
    std::array<u32, 4> m_cascadeDraws{}; ///< tiles drawn since start (debug UI, tests)
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

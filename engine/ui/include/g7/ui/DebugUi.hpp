#pragma once

// Debug and editor UI on Dear ImGui (ADR 0015). ImGui stays private to this module: callers use
// DebugUi and the panels declared here; the backends feed platform::Input and draw via the RHI.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/PostProcess.hpp>
#include <g7/ui/EditorPanel.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace g7::platform
{
class Input;
}

namespace g7::render
{
class ShaderLibrary;
}

namespace g7::ui
{
/// Values shown and edited in the engine panel ("Gothar" window). The engine fills it each frame
/// and applies the edited fields afterwards.
struct EnginePanel
{
    // Shown
    render::FrameStats frame;
    Vec3 cameraPosition{0.0f};
    u32 width = 0;
    u32 height = 0;
    u64 simulationTicks = 0;
    // Edited
    f32 fovDegrees = 70.0f;
    f32 flySpeed = 10.0f;
    render::Tonemapper tonemapper = render::Tonemapper::Aces;
    f32 exposure = 1.0f;
    f32 fogStart = 30.0f;
    f32 fogDensity = 0.0f;
    bool sun = true;
    bool shadowDebug = false;
    bool debugDraw = false;
    bool paused = false;
    f32 timeScale = 1.0f;
    f32 hour = 8.0f;          ///< game time of day (0 .. 24)
    f32 minuteSeconds = 4.0f; ///< real seconds per game minute
};

/// The player figure's animation (M6, "Animation" window): shown only.
struct AnimationPanel
{
    std::string figure;
    std::string graph;
    std::string state;
    std::string previousState; ///< fading out while fade < 1
    f32 fade = 1.0f;
    f32 progress = 0.0f;                             ///< 0..1 one-shots, cycles for loops
    f32 rate = 1.0f;                                 ///< speed-matched playback rate
    std::vector<std::pair<std::string, f32>> clips;  ///< what shapes the pose, with weights
    std::vector<std::pair<std::string, f32>> params; ///< parameters set by the gameplay
    std::vector<std::string> events;                 ///< last events, newest first
    // Trying things out (edited; the engine applies them afterwards)
    std::vector<std::string> sockets; ///< shown: the figure's socket bones
    bool showSockets = false;
    std::string stickSocket; ///< a test stick in this socket, empty: none
    std::string expression;  ///< "angry", "friendly", "fear", "pain", "sleep" or empty
    f32 expressionWeight = 1.0f;
    bool talking = false;
    bool lookAtCamera = false;
    f32 lookYaw = 0.0f; ///< shown, degrees
    f32 lookPitch = 0.0f;
    // Outfit (figures assembled from parts, M6 D2): shown only with `outfit`; edited.
    bool outfit = false;
    std::vector<std::string> heads; ///< head parts that fit the body
    std::string head;               ///< worn now
    struct Garment
    {
        std::string path;
        bool worn = false;
    };
    std::vector<Garment> garments; ///< pieces of the kits fitted to the body (clothing, armour, headgear)
    std::string outfitError;       ///< shown: the last swap that failed
};

/// The script console (M7, "Console" window): output lines, an input line with history.
struct ConsolePanel
{
    std::vector<std::string> lines;   ///< shown, oldest first ("> input", results, "! errors")
    std::vector<std::string> history; ///< earlier inputs, oldest first (Up/Down)
    bool focus = false;               ///< give the input line the keyboard
    bool open = true;                 ///< out: false when the window was closed
    std::string submitted;            ///< out: the line entered with Enter
};

/// Animals (M6 D3, "Creatures" window): spawn, actions, showcase. The engine fills it and reads the edits.
struct CreaturesPanel
{
    std::vector<std::string> species;
    std::string speciesChoice; ///< edited
    bool spawn = false;        ///< out: "spawn in front" pressed
    bool removeAll = false;    ///< out
    struct Row
    {
        u32 id = 0;
        std::string label;
        std::string state;
        f32 speed = 0.0f; ///< edited
        f32 maxSpeed = 1.0f;
        bool showcase = false; ///< edited
        std::string action;    ///< out: the action button pressed ("attack_1", "turn_l" ...)
        std::vector<std::string> events;
    };
    std::vector<Row> rows;
};

/// The hero's inventory (M8, "Inventory" window, Tab) until the real inventory screen (M13): items by
/// category, equipment, values. The engine fills it and acts on the button pressed.
struct InventoryPanel
{
    std::string title;              ///< "Held - Stufe 2"
    std::vector<std::string> stats; ///< lines shown above the list ("LP 40/40", "Stärke 10" ...)
    struct Row
    {
        std::string item;     ///< instance name
        std::string name;     ///< display name
        std::string category; ///< kItemCategories
        u32 count = 0;
        std::string equipped; ///< slot name, empty: not equipped
        bool equippable = false;
    };
    std::vector<Row> rows;
    std::string message; ///< shown: the result of the last action ("needs str 15")
    bool open = true;    ///< out: false when the window was closed
    /// An open chest (M8 part C): its contents next to the hero's; the hero's rows then offer "put" instead
    /// of "drop", the chest's "take".
    bool container = false;
    std::string containerTitle;
    std::vector<Row> containerRows;
    // out: the button pressed this frame
    std::string action; ///< "equip", "unequip", "drop", "take", "put"
    std::string actionItem;
};

/// Lockpicking (M8 part C, "Lockpick" window): progress through the combination, picks left, the last result.
struct LockpickPanel
{
    std::string title;  ///< the mob's name
    usize progress = 0; ///< steps done
    usize length = 0;   ///< steps of the combination
    u32 picks = 0;      ///< lockpicks the hero has
    std::string hint;   ///< keys to use
    std::string result; ///< the last turn ("Das Schloss gibt nach.", "Der Dietrich ist abgebrochen.")
    // out: buttons
    char turn = 0; ///< 'L' or 'R'
    bool leave = false;
};

/// Window-space clip rectangle (x0, y0, x1, y1; +Y down, in framebuffer pixels) as a GL scissor
/// rectangle (origin bottom-left), clamped to the framebuffer; nullopt if nothing remains.
[[nodiscard]] std::optional<render::PixelRect> scissorFromClip(const Vec4& clip, u32 framebufferWidth,
                                                               u32 framebufferHeight) noexcept;

class DebugUi
{
public:
    /// One ImGui context. Without a device (nullptr, tests) frames run but nothing is drawn.
    /// `scale` enlarges fonts and sizes for high-DPI displays (Window::displayScale).
    [[nodiscard]] static Result<DebugUi> create(render::Device* device, render::ShaderLibrary* shaders,
                                                f32 scale = 1.0f);

    DebugUi();
    ~DebugUi();
    DebugUi(DebugUi&&) noexcept;
    DebugUi& operator=(DebugUi&&) noexcept;
    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    /// Starts a frame: feeds this frame's input. `size` in window coordinates (the mouse's space),
    /// `pixels` the framebuffer size.
    void beginFrame(const platform::Input& input, Vec2 size, Vec2 pixels, f32 deltaSeconds);
    /// Draws the engine panel; edits are written back into `panel`.
    void enginePanel(EnginePanel& panel);
    /// Window "Animation" with the player figure's state machine (M6).
    void animationPanel(AnimationPanel& panel);
    /// Window "Console" (M7).
    void consolePanel(ConsolePanel& panel);
    /// Window "Creatures" (M6 D3).
    void creaturesPanel(CreaturesPanel& panel);
    /// Window "Inventory" (M8).
    void inventoryPanel(InventoryPanel& panel);
    /// Window "Lockpick" (M8 part C).
    void lockpickPanel(LockpickPanel& panel);
    /// A text centred at `screen` (window coordinates, +Y down) over everything, without a window: the name
    /// of the focused object (M8) until the HUD exists (M13).
    void focusLabel(Vec2 screen, std::string_view text);
    /// Draws the editor windows (tools/editor); edits and actions are written back into `panel`.
    void editorPanel(EditorPanel& panel);
    /// Finishes the frame and, with a device, draws it into the bound target (normally the window).
    void endFrame(render::Device* device);

    /// ImGui uses the mouse / keyboard / wants text this frame: the game should ignore them.
    [[nodiscard]] bool wantsMouse() const noexcept;
    [[nodiscard]] bool wantsKeyboard() const noexcept;
    [[nodiscard]] bool wantsText() const noexcept;
    [[nodiscard]] bool valid() const noexcept { return m_impl != nullptr; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::ui

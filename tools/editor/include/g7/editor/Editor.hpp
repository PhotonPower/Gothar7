#pragma once

// Editor mode (gothar --editor, M4 foundation, docs/modules/tools.md): select vobs (click, list),
// move/turn/scale them with own gizmos, edit their properties, place models, delete, duplicate and
// save the world. Undo, waynet, zones and play-in-editor follow in M16.
//
// Mouse: left = select and drag gizmo handles, right = look (fly camera). Keys (outside text fields):
// 1/2/3 move/turn/scale, L local/world axes, X snapping, Del delete, Ctrl+D duplicate, Ctrl+S save,
// Esc deselect.

#include <g7/core/Result.hpp>
#include <g7/core/Transform.hpp>
#include <g7/editor/Gizmo.hpp>
#include <g7/runtime/EngineTool.hpp>
#include <g7/ui/EditorPanel.hpp>
#include <g7/world/Components.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace g7::editor
{
class Editor final : public EngineTool
{
public:
    /// Switches the engine into editing: simulation paused, debug UI and overlay on.
    explicit Editor(Engine& engine);

    void update(Engine& engine, f64 realSeconds, bool uiMouse, bool uiKeyboard) override;
    void ui(Engine& engine, ui::DebugUi& debugUi) override;

    // --- Operations (also used by the windows, keys and tests) ---
    void select(world::VobId id);
    [[nodiscard]] world::VobId selection() const noexcept { return m_selection; }
    /// A new mesh vob for `meshPath` on the ground about 8 m in front of the camera; selected.
    [[nodiscard]] Result<world::VobId> place(std::string_view meshPath);
    [[nodiscard]] Result<world::VobId> duplicateSelection();
    void deleteSelection();
    /// Moves, turns or scales the selection to `world` (as a drag does).
    void setSelectionTransform(const Transform& world);
    /// Writes the world back to its source file (loose files only). The first save of a session
    /// keeps the previous version as <file>.bak (there is no undo yet).
    [[nodiscard]] Result<void> save();

    [[nodiscard]] bool dirty() const noexcept { return m_dirty; }
    [[nodiscard]] const std::string& status() const noexcept { return m_status; }
    /// Warning about editing the vob (rewritten by the world's generator), or empty.
    [[nodiscard]] std::string warningFor(world::VobId id) const;

    Gizmo& gizmo() noexcept { return m_gizmo; }
    bool snap = true;
    f32 snapMove = 0.5f;   ///< metres
    f32 snapAngle = 15.0f; ///< degrees
    bool localAxes = false;

private:
    [[nodiscard]] GizmoView view() const;
    void placeGizmo();
    void drag(const Vec2& mouse);
    void drawSelection();
    void sceneChanged();
    void fillPanel();
    void applyPanel();

    Engine& m_engine;
    world::VobId m_selection;
    Gizmo m_gizmo;
    GizmoHandle m_hover = GizmoHandle::None;
    GizmoHandle m_dragHandle = GizmoHandle::None;
    Vec2 m_dragStart{0.0f};
    Transform m_dragFrom;
    Gizmo m_dragGizmo;
    bool m_dirty = false;
    bool m_backedUp = false;
    std::string m_status;
    std::vector<std::string> m_assets;
    ui::EditorPanel m_panel;
};
} // namespace g7::editor

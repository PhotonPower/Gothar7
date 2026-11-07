#pragma once

// Editor mode (gothar --editor, M4 foundation, docs/modules/tools.md): select vobs (click, list),
// move/turn/scale them with own gizmos, edit their properties, place models, delete, duplicate and
// save the world; undo and redo of every edit (M16 part A, ADR 0022), with a question before generated vobs
// are edited or saved. Waynet, zones and play-in-editor follow in M16.
//
// Mouse: left = select and drag gizmo handles, right = look (fly camera). Keys (outside text fields):
// 1/2/3 move/turn/scale, L local/world axes, X snapping, Del delete, Ctrl+D duplicate, Ctrl+S save,
// Ctrl+Z undo, Ctrl+Y (Ctrl+Shift+Z) redo,
// Esc deselect.

#include <g7/core/Result.hpp>
#include <g7/core/Transform.hpp>
#include <g7/editor/Commands.hpp>
#include <g7/editor/Gizmo.hpp>
#include <g7/runtime/EngineTool.hpp>
#include <g7/ui/EditorPanel.hpp>
#include <g7/world/Components.hpp>

#include <functional>
#include <set>
#include <span>
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
    /// keeps the previous version as <file>.bak. With changed generated vobs it asks first (confirmation()).
    [[nodiscard]] Result<void> save();

    /// The world differs from its saved (or loaded) version.
    [[nodiscard]] bool dirty() const noexcept { return !m_history.atSaved(); }
    [[nodiscard]] const std::string& status() const noexcept { return m_status; }
    /// Warning about editing the vob (rewritten by the world's generator), or empty.
    [[nodiscard]] std::string warningFor(world::VobId id) const;

    // --- Undo and redo (M16 part A) ---
    [[nodiscard]] Result<bool> undo();
    [[nodiscard]] Result<bool> redo();
    [[nodiscard]] const History& history() const noexcept { return m_history; }
    /// The question waiting for an answer (empty: none): editing a generated vob the first time in the
    /// session, saving a world whose generated vobs were changed. An edit or save asked about is not done
    /// until confirmed.
    [[nodiscard]] const std::string& confirmation() const noexcept { return m_confirm; }
    /// Answers it: yes does what was asked about (and later edits of generated vobs need no more question).
    void confirm(bool yes);

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
    void applyFields(const std::vector<ui::EditorField>& fields);
    /// Does `change` as one undoable command on the vobs `ids` (and those it creates); asks first if one of
    /// them is generated and that was not confirmed yet in this session.
    Result<void> edit(std::string label, std::vector<world::VobId> ids,
                      const std::function<Result<void>()>& change, bool mergeable = false);
    /// May the vobs be edited now? Else the question is asked and `retry` runs on yes.
    [[nodiscard]] bool mayEdit(std::span<const world::VobId> ids, std::function<void()> retry);
    void ask(std::string question, std::function<void()> onYes);
    [[nodiscard]] Result<void> write();
    [[nodiscard]] std::string vobLabel(world::VobId id) const;

    Engine& m_engine;
    world::VobId m_selection;
    Gizmo m_gizmo;
    GizmoHandle m_hover = GizmoHandle::None;
    GizmoHandle m_dragHandle = GizmoHandle::None;
    Vec2 m_dragStart{0.0f};
    Transform m_dragFrom;
    Gizmo m_dragGizmo;
    bool m_backedUp = false;
    History m_history;
    bool m_generatedConfirmed = false; ///< edits of generated vobs asked about once per session
    std::set<u64> m_generatedEdited;   ///< generated vobs changed since the last save
    std::string m_confirm;
    std::function<void()> m_onYes;
    std::vector<VobState> m_dragBefore; ///< the dragged vob before the drag (one command at its end)
    std::string m_status;
    std::vector<std::string> m_assets;
    ui::EditorPanel m_panel;
};
} // namespace g7::editor

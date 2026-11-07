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
#include <g7/editor/TriggerEdit.hpp>
#include <g7/editor/WaynetCheck.hpp>
#include <g7/editor/WaynetEdit.hpp>
#include <g7/editor/ZoneEdit.hpp>
#include <g7/runtime/EngineTool.hpp>
#include <g7/ui/EditorPanel.hpp>
#include <g7/world/Components.hpp>

#include <functional>
#include <map>
#include <optional>
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

    // --- Waynet (M16 part B): every edit undoable; generated entries ask first (once per session) ---
    [[nodiscard]] bool waynetMode() const noexcept { return m_waynetMode; }
    void setWaynetMode(bool on);
    void selectWaynet(std::string name);
    [[nodiscard]] const std::string& waynetSelection() const noexcept { return m_waynetSelected; }
    /// A new way point at `feet` (name from the selected point's stem, else the world's), joined to the
    /// selected way point; selected.
    [[nodiscard]] Result<std::string> addWaypoint(const Vec3& feet);
    /// A new freepoint FP_<TYPE>_<stem>_NN at `feet` facing `yawDegrees` (0 = -Z, positive left); selected.
    [[nodiscard]] Result<std::string> addFreepoint(std::string_view type, const Vec3& feet, f32 yawDegrees);
    [[nodiscard]] Result<void> moveWaynetSelection(const Vec3& feet,
                                                   std::optional<f32> yawDegrees = std::nullopt);
    [[nodiscard]] Result<void> renameWaynetSelection(std::string name);
    /// Deletes the selection; for a generated point waynetAnnotation() then holds the line for welt's
    /// annotations (decision 2: shown to copy, not written).
    [[nodiscard]] Result<void> deleteWaynetSelection();
    /// Joins the two way points, or parts them if joined; true if joined now.
    [[nodiscard]] Result<bool> toggleEdge(std::string_view a, std::string_view b);
    /// Runs the checks (world.md recommendations) against the world's collision.
    [[nodiscard]] const std::vector<WaynetIssue>& checkWaynet();
    /// The path finding's way between two way points (names), drawn until the next probe; nullopt:
    /// unconnected.
    [[nodiscard]] std::optional<std::vector<std::string>> probePath(std::string_view from,
                                                                    std::string_view to);
    [[nodiscard]] const std::string& waynetAnnotation() const noexcept { return m_annotation; }
    [[nodiscard]] const world::WaynetData& waynetData() const noexcept { return m_waynetData; }

    // --- Zones and triggers (M16 part C): every zone edit undoable; the engine follows at once ---
    [[nodiscard]] bool zoneMode() const noexcept { return m_zoneMode; }
    void setZoneMode(bool on);
    void selectZone(std::optional<usize> index);
    [[nodiscard]] std::optional<usize> zoneSelection() const noexcept { return m_zoneSelected; }
    [[nodiscard]] const std::vector<world::Zone>& zones() const noexcept { return m_zones; }
    /// A new zone (indoor, music, ambient) at `centre`; selected. Its index.
    [[nodiscard]] Result<usize> addZone(std::string type, std::string value, const Vec3& centre);
    [[nodiscard]] Result<void> setZoneBox(const world::ZoneBox& box); ///< of the selection
    [[nodiscard]] Result<void> setZoneValue(std::string value);       ///< of the selection
    [[nodiscard]] Result<void> deleteZone();                          ///< the selection
    /// Index of the zone of `type` that counts at `point` (the smallest holding it); nullopt: none.
    [[nodiscard]] std::optional<usize> zoneAt(std::string_view type, const Vec3& point) const;
    /// What `value` may be for a zone type (music themes, ambiences, the world's room tags).
    [[nodiscard]] std::vector<std::string> zoneValueChoices(std::string_view type) const;
    /// Checks the scene's triggers (script functions, level changes).
    [[nodiscard]] const std::vector<TriggerIssue>& checkTriggers();
    /// The start points of a world (VFS path; the open world: from the scene); nullopt: no such world.
    [[nodiscard]] std::optional<std::vector<std::string>> startsOf(std::string_view world);

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
    void drawGizmo(); ///< m_gizmo with m_hover / m_dragHandle lit
    void sceneChanged();
    void historyApplied(); ///< after undo or redo: rendering and waynet follow
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
    /// Does `change` on the working waynet as one undoable command on the entries `names` and `edges` (and
    /// the edges at those points); asks first for generated entries; hands the result to the engine.
    Result<void> editWaynet(std::string label, std::vector<std::string> names,
                            std::vector<std::pair<std::string, std::string>> edges,
                            const std::function<Result<void>(world::WaynetData&)>& change,
                            bool mergeable = false);
    void waynetMouse(Engine& engine, const Vec2& mouse, bool pressed, bool held);
    void drawWaynet();
    void fillWaynetPanel();
    Result<void> editZones(std::string label,
                           const std::function<Result<void>(std::vector<world::Zone>&)>& change,
                           bool mergeable = false);
    void zoneMouse(Engine& engine, const Vec2& mouse, bool pressed, bool held);
    void drawZones();
    void drawTriggers();
    void fillZonePanel();
    void applyZonePanel();
    void applyWaynetPanel();
    [[nodiscard]] std::optional<Vec3> groundUnder(const Vec2& mouse) const;

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
    // Waynet (part B)
    world::WaynetData m_waynetData; ///< the world's waynet as edited (handed to the engine after each edit)
    bool m_waynetMode = false;
    f32 m_waynetRadius = 80.0f; ///< points drawn and picked this near the camera ([editor] waynet_radius)
    std::string m_waynetSelected;
    bool m_waynetConfirmed = false;                ///< edits of generated waynet entries asked about once
    std::set<std::string> m_waynetGeneratedEdited; ///< generated entries changed since the last save
    std::string m_annotation;
    std::vector<WaynetIssue> m_issues;
    std::vector<Vec3> m_probe; ///< the last probed path
    std::optional<WaynetSnapshot> m_waynetDragBefore;
    bool m_waynetDragging = false;
    ui::WaynetPanel m_waynetPanel;
    // Zones and triggers (part C)
    std::vector<world::Zone> m_zones; ///< the world's zones as edited (handed to the engine after each edit)
    bool m_zoneMode = false;
    std::optional<usize> m_zoneSelected;
    bool m_zoneDragging = false;
    std::optional<std::vector<world::Zone>> m_zoneDragBefore;
    Transform m_zoneDragFrom;
    std::vector<TriggerIssue> m_triggerIssues;
    std::vector<std::string> m_worlds;          ///< .g7world files in the VFS
    std::vector<std::string> m_scriptFunctions; ///< for onEnter/onLeave
    std::map<std::string, std::optional<std::vector<std::string>>, std::less<>> m_starts; ///< per world
    ui::ZonePanel m_zonePanel;
};
} // namespace g7::editor

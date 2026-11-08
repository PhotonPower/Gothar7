#pragma once

// Undo and redo of the editor (M16 part A, ADR 0022, docs/modules/editor.md): every edit is a command that
// keeps the state of the vobs it touched before and after, as the world file writes them (writeVobJson). Undo
// puts the "before" back, redo the "after" - byte for byte, with one mechanism for every kind of vob.

#include <g7/core/Result.hpp>
#include <g7/editor/TerrainBrush.hpp>
#include <g7/editor/WaynetEdit.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/Components.hpp>
#include <g7/world/WorldFile.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace g7::world
{
class Scene;
struct WorldFile;
} // namespace g7::world

namespace g7::editor
{
/// A vob's state: its line of the world file, or nullopt when it does not exist.
struct VobState
{
    world::VobId id;
    std::optional<std::string> json;

    friend bool operator==(const VobState&, const VobState&) = default;
};

/// The states of `ids` and of all their descendants (parents before children); a missing id is "not there".
[[nodiscard]] std::vector<VobState> captureVobs(const world::Scene& scene, std::span<const world::VobId> ids);
/// Makes the scene match `states`: vobs that are "not there" are destroyed, the others restored (parents
/// first); ids stay used. Fails (and stops) when a state cannot be restored, e.g. its parent is gone.
[[nodiscard]] Result<void> restoreVobs(world::Scene& scene, std::span<const VobState> states);

/// Where terrain patches go back to (part F2): the heightfield and the splat maps' bytes.
struct TerrainTarget
{
    world::Heightfield* heights = nullptr;
    std::vector<SplatPixels> maps;
};

/// One undoable edit.
struct Command
{
    std::string label;            ///< what the user did ("move HUT_A")
    std::vector<VobState> before; ///< every vob it touched, as it was
    std::vector<VobState> after;  ///< and as it is now
    /// Typing in a field: the next edit of the same vobs (and waynet entries) with the same label joins it.
    bool mergeable = false;
    /// Waynet edits (part B): the touched entries before and after (none: the command left the waynet alone).
    std::optional<WaynetSnapshot> waynetBefore;
    std::optional<WaynetSnapshot> waynetAfter;
    /// Zone edits (part C): the whole zones block before and after (a few hundred boxes at most).
    std::optional<std::vector<world::Zone>> zonesBefore;
    std::optional<std::vector<world::Zone>> zonesAfter;
    /// Routine edits (part E): the routines of the editor's file before and after.
    std::optional<std::vector<RoutineInfo>> routinesBefore;
    std::optional<std::vector<RoutineInfo>> routinesAfter;
    /// Terrain strokes (part F2): the touched rectangle of heights and of the splat maps, before and after.
    std::optional<HeightPatch> heightsBefore;
    std::optional<HeightPatch> heightsAfter;
    std::vector<SplatPatch> splatBefore;
    std::vector<SplatPatch> splatAfter;
};

/// The edits of a session: undo, redo, and whether the world differs from its saved version.
class History
{
public:
    explicit History(usize depth = 200) : m_depth(depth) {}

    /// Adds a command that is already done. Redo is gone; a mergeable command joins the last one if that is
    /// mergeable too, has the same label and touched the same vobs. Beyond the depth the oldest is dropped.
    void record(Command command);
    /// Puts the last command's "before" back; false if there is none (or it failed - then the history stays).
    [[nodiscard]] Result<bool> undo(world::Scene& scene, world::WaynetData* waynet = nullptr,
                                    std::vector<world::Zone>* zones = nullptr,
                                    std::vector<RoutineInfo>* routines = nullptr,
                                    TerrainTarget* terrain = nullptr);
    /// Puts the next undone command's "after" back.
    [[nodiscard]] Result<bool> redo(world::Scene& scene, world::WaynetData* waynet = nullptr,
                                    std::vector<world::Zone>* zones = nullptr,
                                    std::vector<RoutineInfo>* routines = nullptr,
                                    TerrainTarget* terrain = nullptr);

    /// The last undo or redo put terrain back (the drawn terrain and the collision must follow).
    [[nodiscard]] bool lastTouchedTerrain() const noexcept { return m_lastTerrain; }
    [[nodiscard]] bool canUndo() const noexcept { return m_done > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return m_done < m_commands.size(); }
    /// Label of the command undo/redo would act on (empty: none).
    [[nodiscard]] std::string undoLabel() const;
    [[nodiscard]] std::string redoLabel() const;
    [[nodiscard]] usize size() const noexcept { return m_commands.size(); }

    /// The world was saved now.
    void markSaved() noexcept { m_saved = static_cast<i64>(m_done); }
    /// The world equals its saved version (no command done or undone since).
    [[nodiscard]] bool atSaved() const noexcept { return m_saved == static_cast<i64>(m_done); }
    void clear() noexcept;

private:
    std::vector<Command> m_commands;
    usize m_done = 0; ///< commands applied (the next undo is m_commands[m_done - 1])
    i64 m_saved = 0;  ///< m_done when saved; -1: that version is no longer reachable
    usize m_depth = 200;
    bool m_lastTerrain = false;
};
} // namespace g7::editor

#pragma once

// Data of the editor windows (tools/editor, M4). Plain values only: the editor fills them from the
// world, the ui module draws and edits them with ImGui (ADR 0015: ImGui stays in this module).

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <string>
#include <vector>

namespace g7::ui
{
/// One line of the vob list (depth-first, indented by `depth`).
struct EditorVobRow
{
    u64 id = 0;
    std::string label; ///< "NAME (type)"
    u32 depth = 0;
    bool selected = false;
};

/// One editable property of the selection. The editor reads `changed` after the frame.
struct EditorField
{
    enum class Kind : u8
    {
        ReadOnly, ///< text shown
        Text,
        Number,
        Vector, ///< three numbers (position, Euler degrees, scale, extents)
        Color,  ///< linear colour
        Flag,
        Choice, ///< one of `choices`
    };
    std::string label;
    Kind kind = Kind::ReadOnly;
    std::string text;
    f32 number = 0.0f;
    Vec3 vector{0.0f};
    bool flag = false;
    std::vector<std::string> choices;
    i32 choice = 0;
    bool changed = false;
};

struct EditorPanel
{
    // Shown
    std::string world;   ///< VFS path of the world
    std::string status;  ///< last action ("saved ...", errors)
    std::string warning; ///< about the selection (e.g. rewritten by the generator)
    bool dirty = false;
    bool canSave = false;
    std::string undoLabel; ///< what Ctrl+Z undoes (empty: nothing)
    std::string redoLabel;
    std::string confirm; ///< a question waiting for yes/no (editing generated vobs, saving them)
    std::vector<EditorVobRow> vobs;
    std::vector<std::string> assets; ///< placeable models (VFS paths)
    std::vector<EditorField> fields; ///< properties of the selection; edited in place
    // Lights and mobs (M16 part D)
    std::string lightInfo; ///< lights this frame of the budget (E1)
    std::vector<std::string> lightPresets;
    std::vector<std::string> mobIssues;
    // Prefabs (M16 part F)
    std::vector<std::string> prefabs; ///< names of the templates (assets/source/prefabs)

    // Edited
    i32 gizmoMode = 0; ///< 0 move, 1 turn, 2 scale
    bool localAxes = false;
    bool snap = true;
    f32 snapMove = 0.5f;   ///< metres
    f32 snapAngle = 15.0f; ///< degrees
    std::string assetFilter;
    f32 hour = 12.0f; ///< preview time of day (display only)
    i32 lightPreset = 0;
    std::string prefabName; ///< for "save as prefab"

    // Actions of this frame
    u64 clickedVob = 0;  ///< row clicked (0: none)
    i32 placeAsset = -1; ///< index into `assets`
    bool save = false;
    bool remove = false; ///< delete the selection
    bool duplicate = false;
    bool undo = false;
    bool redo = false;
    bool confirmYes = false;
    bool confirmNo = false;
    bool hourChanged = false;
    bool addLight = false; ///< a light of `lightPreset` ahead of the camera
    bool checkMobs = false;
    i32 clickedMobIssue = -1;
    i32 placePrefab = -1;    ///< index into `prefabs`, placed ahead of the camera
    bool savePrefab = false; ///< the selection (with children) as `prefabName`
};

/// The waynet window of the editor (M16 part B, docs/modules/editor.md).
struct WaynetPanel
{
    // Shown
    bool shown = false;   ///< the editor is in waynet mode
    std::string selected; ///< name of the selected point or freepoint (empty: none)
    bool selectedFreepoint = false;
    bool selectedGenerated = false;
    std::string annotation; ///< for a deleted generated point: the line for welt's waynet.json "remove"
    std::vector<std::string> freepointTypes;
    std::vector<std::string> issues; ///< of the last check
    std::string pathResult;          ///< of the last path probe

    // Edited
    bool active = false;   ///< waynet mode on (key G)
    Vec3 position{0.0f};   ///< of the selection
    f32 yawDegrees = 0.0f; ///< facing of a freepoint
    std::string name;      ///< rename field
    i32 type = 0;          ///< freepoint type for "add freepoint"
    std::string pathFrom;  ///< path probe
    std::string pathTo;

    // Actions of this frame
    bool moved = false; ///< position or facing edited
    bool rename = false;
    bool addPoint = false;
    bool addFreepoint = false;
    bool remove = false;
    bool check = false;
    bool probe = false;
    i32 clickedIssue = -1;
};

/// The zones and triggers window of the editor (M16 part C, docs/modules/editor.md).
struct ZonePanel
{
    // Shown
    bool shown = false;
    std::vector<std::string> rows; ///< every zone ("music LAGER (40 x 20 x 40 m)")
    i32 selected = -1;             ///< row of the selection
    bool selectedEditable = false; ///< a known type with a box (others are only shown)
    std::string selectedType;
    std::vector<std::string> values;    ///< what the selection's value may be (indoor: free text as well)
    std::vector<std::string> types;     ///< for "add zone"
    std::vector<std::string> newValues; ///< values for the type chosen for "add zone"
    std::string here;                   ///< which zone counts at the camera, per type
    std::vector<std::string> triggerIssues;

    // Edited
    bool active = false; ///< zone mode on (key V)
    Vec3 center{0.0f};
    Vec3 halfExtents{1.0f};
    f32 yawDegrees = 0.0f;
    std::string value; ///< of the selection
    i32 newType = 0;
    std::string newValue;

    // Actions of this frame
    i32 clickedRow = -1;
    bool boxChanged = false;
    bool valueChanged = false;
    bool add = false;
    bool remove = false;
    bool checkTriggers = false;
    i32 clickedIssue = -1;
};

/// The routines window of the editor (M16 part E, docs/modules/editor.md): ghosts, the day plans, editing the
/// routines of the editor's own file, play-in-editor.
struct RoutinePanel
{
    // Shown
    bool shown = false;
    bool playing = false;
    std::vector<std::string> npcs;     ///< "npc_x: rtn_y - zs_sweep @ wp_z"
    std::vector<std::string> routines; ///< every routine; editable ones marked
    i32 selectedRoutine = -1;
    bool editable = false;            ///< the selected routine lives in the editor's file
    std::string file;                 ///< of the selected routine
    std::string editorFile;           ///< routines/<site>_editor.lua
    bool dirty = false;               ///< the editor's routines differ from the file
    std::vector<std::string> entries; ///< of the selected routine
    i32 selectedEntry = -1;
    std::vector<std::string> states; ///< zs_ states of the scripts
    std::vector<std::string> issues;

    // Edited
    bool ghosts = true;
    f32 hour = 12.0f; ///< preview time (not the game's)
    std::string from = "08:00";
    std::string to = "20:00";
    i32 state = 0;
    std::string at;
    bool pickPlace = false; ///< the next click in the world picks a way point or freepoint for `at`
    std::string newName = "rtn_";

    // Actions of this frame
    i32 clickedNpc = -1;
    i32 clickedRoutine = -1;
    i32 clickedEntry = -1;
    bool addEntry = false;
    bool changeEntry = false;
    bool removeEntry = false;
    bool newRoutine = false;
    bool write = false;
    bool play = false;        ///< F5: at the camera
    bool playAtStart = false; ///< Shift+F5
    bool stop = false;
};

/// The terrain window of the editor (M16 part F2): the height brush and splat painting.
struct TerrainPanel
{
    // Shown
    bool shown = false;
    std::string locked; ///< why the terrain cannot be edited (empty: it can)
    std::vector<std::string> layers;
    bool dirty = false; ///< heights or weights differ from the files

    // Edited
    bool active = false; ///< terrain mode on (key T)
    i32 mode = 0;        ///< raise, lower, smooth, flatten, paint
    f32 radius = 4.0f;
    f32 strength = 2.0f;
    f32 flattenHeight = 0.0f;
    i32 layer = 0;
};
} // namespace g7::ui

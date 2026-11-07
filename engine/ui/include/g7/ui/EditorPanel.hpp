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

    // Edited
    i32 gizmoMode = 0; ///< 0 move, 1 turn, 2 scale
    bool localAxes = false;
    bool snap = true;
    f32 snapMove = 0.5f;   ///< metres
    f32 snapAngle = 15.0f; ///< degrees
    std::string assetFilter;

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
} // namespace g7::ui

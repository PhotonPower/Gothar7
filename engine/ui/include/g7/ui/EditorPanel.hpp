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
};
} // namespace g7::ui

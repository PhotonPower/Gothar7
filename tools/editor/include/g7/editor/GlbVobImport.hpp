#pragma once

// Vobs from Blender (M16 part F, ADR 0023): a .glb whose empty nodes are named VOB_<type>_<name> is placed as
// a group - the model as a mesh vob, each marker as its child vob (contract docs/modules/asset.md
// "VOB_-Knoten"). Types: LIGHT (a light, range from the empty's size), START (a start point), TRIGGER (a box
// trigger, half extents = the empty's scale). Blender's ".001" endings are dropped.

#include <g7/core/Math.hpp>
#include <g7/world/WorldFile.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::editor
{
/// One VOB_ node: its name and placement in the model (the .glb's space).
struct VobMarker
{
    std::string name;
    Mat4 local{1.0f};
};

struct VobImport
{
    std::vector<world::WorldFileVob>
        vobs;                          ///< children of the model's vob (ids and parent left to the caller)
    std::vector<std::string> warnings; ///< markers that were skipped and why
};

/// The vobs of the markers, placed relative to the model (no scale on the vob: it sizes the volume instead).
[[nodiscard]] VobImport vobsFromMarkers(std::span<const VobMarker> markers);
} // namespace g7::editor

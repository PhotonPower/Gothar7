#pragma once

// Vobs from Blender (M16 part F, ADR 0023): a .glb whose empty nodes are named VOB_<type>_<name> is placed as
// a group - the model as a mesh vob, each marker as its child vob (contract docs/modules/asset.md
// "VOB_-Knoten"). Types: LIGHT (a light, range from the empty's size), START (a start point), TRIGGER (a box
// trigger, half extents = the empty's scale) and, for the dungeon (D7, docs/modules/dungeon.md), PLATE (a box
// trigger with `plate`), TELEPORT (a box trigger with `teleport`), RELAY and SPAWN. Blender's ".001" endings
// are dropped. The node's glTF `extras` (Blender's custom properties) fill the vob's component; a mesh node
// with `extras.mover` makes the placed model a moving part.

#include <g7/core/Math.hpp>
#include <g7/world/WorldFile.hpp>

#include <optional>
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
    std::string extras; ///< the node's glTF extras as JSON text (empty: none)
};

struct VobImport
{
    std::vector<world::WorldFileVob>
        vobs;                          ///< children of the model's vob (ids and parent left to the caller)
    std::vector<std::string> warnings; ///< markers that were skipped and why, extras that were ignored
};

/// The vobs of the markers, placed relative to the model (no scale on the vob: it sizes the volume instead).
/// Extras (dungeon D7): their keys are the fields of the component in the .g7world (`targets`, `action`,
/// `mode`, `to`, `npc` ...; a trigger's level change as `world`, `start`, `fall`; a plate's `weight` and
/// `release_action`). Blender's ways are understood: `targets` may be one text "A, B", flags may be 0 or 1.
/// Unknown keys are warned about and left out; a vob whose fields are wrong is skipped with a warning.
[[nodiscard]] VobImport vobsFromMarkers(std::span<const VobMarker> markers);

/// `extras.mover` of a mesh node as the component (an object of its fields, or just the kind as text);
/// nullopt with a warning in `warnings` if it is wrong. `node` names it in the warning.
[[nodiscard]] std::optional<world::Mover> moverFromExtras(std::string_view extras, std::string_view node,
                                                          std::vector<std::string>& warnings);
} // namespace g7::editor

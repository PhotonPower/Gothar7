#pragma once

// A world as it is now against its .g7world file (M15 part B, docs/modules/save.md): which file vobs are
// gone, which changed (a door opened, a vob moved), which world vobs are new, and the runtime vobs (items
// dropped). Each entry names its vob, so a delta that no longer fits a regenerated file is dropped there
// instead of changing another vob.

#include <g7/core/Types.hpp>
#include <g7/world/WorldFile.hpp>

#include <string>
#include <vector>

namespace g7::save
{
struct RemovedVob
{
    world::VobId id;
    std::string name; ///< as in the file (checked when applied)
    friend bool operator==(const RemovedVob&, const RemovedVob&) = default;
};

struct WorldDelta
{
    std::vector<RemovedVob> removed;
    std::vector<world::WorldFileVob> changed; ///< file vobs as they are now, and new world-range vobs
    std::vector<world::WorldFileVob> runtime; ///< runtime vobs (dropped items), with their runtime ids
    u64 nextVobId = 1;
    u64 nextRuntimeVobId = world::kRuntimeVobIdBase;
};

/// `now`: the world-range vobs of the scene and its counter (world::captureWorld). Vobs are compared as the
/// file writes them. The runtime vobs are the engine's to add.
[[nodiscard]] WorldDelta diffWorld(const world::WorldFile& base, const world::WorldFile& now);
/// The file with the delta applied (without the runtime vobs: the engine spawns those). Entries whose vob in
/// `base` has another name or type are dropped (logged); `dropped` counts them.
[[nodiscard]] world::WorldFile applyDelta(world::WorldFile base, const WorldDelta& delta,
                                          usize* dropped = nullptr);
} // namespace g7::save

#pragma once

// The world format .g7world (docs/modules/world.md; contract with the world track). Text (JSON,
// ADR 0017) so worlds diff well under version control; JSON types stay inside this module.

#include <g7/core/Result.hpp>
#include <g7/core/Transform.hpp>
#include <g7/world/Components.hpp>
#include <g7/world/TerrainRef.hpp>
#include <g7/world/Waynet.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace g7::asset
{
class Vfs;
}

namespace g7::world
{
inline constexpr u32 kWorldFileVersion = 1;

enum class VobType : u8
{
    Empty,   ///< grouping node (e.g. a hut whose parts are children)
    Mesh,    ///< MeshRef
    Light,   ///< LightSource
    Start,   ///< StartPoint
    Sound,   ///< SoundEmitter
    Trigger, ///< TriggerVolume
    Mob,     ///< MobRef + MeshRef (placeholder until M8)
    Water,   ///< WaterVolume (M5)
    Item,    ///< ItemRef (M8)
};

/// One vob as stored in the file.
struct WorldFileVob
{
    VobId id;
    VobType type = VobType::Empty;
    std::string name;
    VobId parent;                             ///< 0 = root
    Transform transform;                      ///< relative to the parent
    std::string mesh;                         ///< VFS path; Mesh and Mob vobs
    VobCategory category = VobCategory::Deco; ///< Mesh vobs; Mob vobs are always Gameplay
    LightSource light;                        ///< Light vobs only
    SoundEmitter sound;                       ///< Sound vobs only
    TriggerVolume trigger;                    ///< Trigger vobs only
    MobRef mob;                               ///< Mob vobs only
    WaterVolume water;                        ///< Water vobs only
    ItemRef item;                             ///< Item vobs only
};

/// A room's box (zone type "indoor", world.md "Zonen"): turned by `yaw` degrees about +Y like a vob; local +X
/// points to (cos yaw, 0, -sin yaw).
struct ZoneBox
{
    Vec3 center{0.0f};
    Vec3 halfExtents{1.0f};
    f32 yawDegrees = 0.0f;
};

struct Zone
{
    std::string type;  ///< "indoor", "music", "ambient"; other types are kept unread
    std::string value; ///< indoor: the room tag LEO_<USE>_<CODE>_INNEN; music: the theme; ambient: its name
    std::optional<ZoneBox> box; ///< indoor, music and ambient zones (several boxes may share a value)
    std::string json;           ///< other types: the entry as it was (written back unchanged)
};

struct WorldFile
{
    std::string name;
    u64 nextVobId = 1;
    std::vector<std::string> staticMeshes;
    /// Optional "terrain" block (version 1).
    std::optional<TerrainRef> terrain;
    std::vector<WorldFileVob> vobs;
    /// Optional "waynet" block (v1, world.md "Wegnetz"): checked and sorted when read.
    std::optional<WaynetData> waynet;
    /// Optional "zones" block (world.md "Zonen"): indoor zones checked and read, the other types kept as they
    /// are; written one per line, sorted by value (indoor ties by box centre x, then z).
    std::vector<Zone> zones;
    /// Optional "generator" head (world.md): written by gothar-worldgen, kept unchanged. `generatorOwned`
    /// holds its "owned" ids as closed ranges - a hint for the editor that such vobs are rewritten by the
    /// next generator run.
    std::string generatorJson;
    std::vector<std::pair<u64, u64>> generatorOwned;
};

/// True if the world's generator rewrites the vob (editor warning); false without a generator head.
[[nodiscard]] bool isGenerated(const WorldFile& world, VobId id) noexcept;

/// Reads a .g7world. Errors name `source` and the entry ("vobs[3]: missing 'id'").
[[nodiscard]] Result<WorldFile> parseWorldFile(std::string_view json, std::string_view source = "<world>");
[[nodiscard]] Result<WorldFile> loadWorldFile(const asset::Vfs& vfs, std::string_view path);
/// Writes stable text: keys in a fixed order, vobs sorted by id, two-space indent, trailing
/// newline - the same world always gives the same bytes.
[[nodiscard]] std::string writeWorldFile(const WorldFile& world);

[[nodiscard]] std::string_view vobTypeName(VobType type) noexcept;
} // namespace g7::world

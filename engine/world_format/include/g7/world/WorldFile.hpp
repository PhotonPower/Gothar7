#pragma once

// The world format .g7world (docs/modules/world.md; contract with the world track). Text (JSON,
// ADR 0017) so worlds diff well under version control; JSON types stay inside this module.

#include <g7/core/Result.hpp>
#include <g7/core/Transform.hpp>
#include <g7/world/Components.hpp>
#include <g7/world/TerrainRef.hpp>
#include <g7/world/Waynet.hpp>

#include <optional>
#include <span>
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
    Relay,   ///< Relay (dungeon D1): passes events on, invisible
    Spawn,   ///< Spawn (dungeon D6): puts NPCs in when the world is new, invisible
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
    Relay relay;                              ///< Relay vobs only
    Spawn spawn;                              ///< Spawn vobs only
    /// `components.mover` (dungeon D2): mesh and mob vobs that move (gate, secret wall, trapdoor).
    std::optional<Mover> mover;
    /// `components.surface.footstep` (M13 E, contract with welt): the footstep material of what one walks on
    /// in this mesh or mob ("wood", "stone" ...); empty: from the engine's path patterns
    /// (data/footsteps.toml).
    std::string footstep;
};

/// A room's box (zone type "indoor", world.md "Zonen"): turned by `yaw` degrees about +Y like a vob; local +X
/// points to (cos yaw, 0, -sin yaw).
struct ZoneBox
{
    Vec3 center{0.0f};
    Vec3 halfExtents{1.0f};
    f32 yawDegrees = 0.0f;
    friend bool operator==(const ZoneBox&, const ZoneBox&) = default;
};

/// The fog of a zone of type "fog" (dungeon D7b): inside its box the camera's fog becomes this, fading in
/// over `fade` metres around the box. Colours are sRGB 0..1 as written (the engine converts them).
struct ZoneFog
{
    Vec3 color{0.0f};
    f32 distance = 20.0f; ///< metres at which the fog hides 90 %
    f32 start = 0.0f;     ///< metres without fog
    f32 fade = 3.0f;      ///< metres around the box over which it fades in
    friend bool operator==(const ZoneFog&, const ZoneFog&) = default;
};

struct Zone
{
    std::string type;  ///< "indoor", "music", "ambient", "fog"; other types are kept unread
    std::string value; ///< indoor: the room tag LEO_<USE>_<CODE>_INNEN; music: the theme; ambient: its name;
                       ///< fog: a name
    std::optional<ZoneBox> box; ///< indoor, music, ambient and fog zones (several boxes may share a value)
    std::string json;           ///< other types: the entry as it was (written back unchanged)
    std::optional<ZoneFog> fog; ///< fog zones (dungeon D7b)
    friend bool operator==(const Zone&, const Zone&) = default;
};

/// `environment` of a world (dungeon D7b, dungeon.md "Umgebung je Welt"): no sky, sun, moon or stars, no
/// time-of-day curves, no weather; a fixed ambient light and fog instead. Without it a world has
/// data/environment.toml's day and night and the weather. Colours are sRGB 0..1 as written.
struct WorldEnvironment
{
    Vec3 ambient{0.05f};               ///< ambient light from above (the "Grundlicht")
    std::optional<Vec3> ambientGround; ///< from below; nullopt: half of `ambient`
    Vec3 fogColor{0.0f};               ///< also the colour of the background
    f32 fogStart = 0.0f;               ///< metres without fog
    f32 fogDistance = 40.0f;           ///< metres at which the fog hides 90 %
    friend bool operator==(const WorldEnvironment&, const WorldEnvironment&) = default;
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
    /// Optional "environment" block (dungeon D7b): the world has no sky and its own light and fog.
    std::optional<WorldEnvironment> environment;
    /// Optional "generator" head (world.md): written by gothar-worldgen, kept unchanged. `generatorOwned`
    /// holds its "owned" ids as closed ranges - a hint for the editor that such vobs are rewritten by the
    /// next generator run.
    std::string generatorJson;
    std::vector<std::pair<u64, u64>> generatorOwned;
};

/// True if the world's generator rewrites the vob (editor warning); false without a generator head.
[[nodiscard]] bool isGenerated(const WorldFile& world, VobId id) noexcept;

/// True if `point` lies in the turned box (edges included).
[[nodiscard]] bool zoneContains(const ZoneBox& box, const Vec3& point) noexcept;
/// The zone of `type` with a box holding `point` and the smallest volume (nested zones: the smallest wins,
/// world.md "Zonen"); null if none. `volume` gets its box volume (half extents multiplied).
[[nodiscard]] const Zone* smallestZone(std::span<const Zone> zones, std::string_view type, const Vec3& point,
                                       f32* volume = nullptr) noexcept;
/// How far `point` lies outside the turned box (0 inside), metres.
[[nodiscard]] f32 zoneDistance(const ZoneBox& box, const Vec3& point) noexcept;
/// The fog zone that counts at `point` and how much (dungeon D7b): inside a box 1 (nested: the smallest),
/// else the nearest whose fade reaches it, 1 at its box fading to 0 at `fade` metres; null if none.
[[nodiscard]] const Zone* fogZoneAt(std::span<const Zone> zones, const Vec3& point, f32& weight) noexcept;
/// Checks one zone as reading does (known types need a value and a box with positive half extents); `where`
/// names it in the message ("zones[3]").
[[nodiscard]] Result<void> checkZone(const Zone& zone, std::string_view where);

/// Reads a .g7world. Errors name `source` and the entry ("vobs[3]: missing 'id'").
[[nodiscard]] Result<WorldFile> parseWorldFile(std::string_view json, std::string_view source = "<world>");
[[nodiscard]] Result<WorldFile> loadWorldFile(const asset::Vfs& vfs, std::string_view path);
/// Writes stable text: keys in a fixed order, vobs sorted by id, two-space indent, trailing
/// newline - the same world always gives the same bytes.
[[nodiscard]] std::string writeWorldFile(const WorldFile& world);

[[nodiscard]] std::string_view vobTypeName(VobType type) noexcept;
/// One vob as the file writes it (one line, stable: equal vobs give equal text) - the save game's world delta
/// (M15) and comparisons.
[[nodiscard]] std::string writeVobJson(const WorldFileVob& vob);
/// Reads what writeVobJson wrote (the same checks as a vob of a file).
[[nodiscard]] Result<WorldFileVob> parseVobJson(std::string_view json, std::string_view source = "<vob>");
} // namespace g7::world

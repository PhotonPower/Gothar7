#pragma once

// The world format .g7world (docs/modules/world.md; contract with the world track). Text (JSON,
// ADR 0017) so worlds diff well under version control; JSON types stay inside this module.

#include <g7/core/Result.hpp>
#include <g7/core/Transform.hpp>
#include <g7/world/Components.hpp>
#include <g7/world/Terrain.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
class Vfs;
}

namespace g7::world
{
class Scene;

inline constexpr u32 kWorldFileVersion = 1;

enum class VobType : u8
{
    Empty, ///< grouping node (e.g. a hut whose parts are children)
    Mesh,  ///< MeshRef
    Light, ///< LightSource
};

/// One vob as stored in the file.
struct WorldFileVob
{
    VobId id;
    VobType type = VobType::Empty;
    std::string name;
    VobId parent;        ///< 0 = root
    Transform transform; ///< relative to the parent
    std::string mesh;    ///< VFS path; Mesh vobs only
    LightSource light;   ///< Light vobs only
};

struct WorldFile
{
    std::string name;
    u64 nextVobId = 1;
    std::vector<std::string> staticMeshes;
    /// Optional "terrain" block (version 1).
    std::optional<TerrainRef> terrain;
    std::vector<WorldFileVob> vobs;
    /// Waynet and zones are kept as JSON text until their systems exist (read and written back
    /// unchanged); empty = absent.
    std::string waynetJson;
    std::string zonesJson;
};

/// Reads a .g7world. Errors name `source` and the entry ("vobs[3]: missing 'id'").
[[nodiscard]] Result<WorldFile> parseWorldFile(std::string_view json, std::string_view source = "<world>");
[[nodiscard]] Result<WorldFile> loadWorldFile(const asset::Vfs& vfs, std::string_view path);
/// Writes stable text: keys in a fixed order, vobs sorted by id, two-space indent, trailing
/// newline - the same world always gives the same bytes.
[[nodiscard]] std::string writeWorldFile(const WorldFile& world);

/// Creates the file's vobs in `scene` with their ids (parents before children, in any file order)
/// and raises the scene's id counter to the file's nextVobId. Fails for duplicate ids, unknown
/// parents and parent cycles; nothing is created then.
[[nodiscard]] Result<void> spawnWorld(Scene& scene, const WorldFile& world);
/// The scene's world vobs (runtime vobs are left out) as a file, e.g. for the editor.
[[nodiscard]] WorldFile captureWorld(const Scene& scene, std::string_view name);

[[nodiscard]] std::string_view vobTypeName(VobType type) noexcept;
} // namespace g7::world

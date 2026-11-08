#pragma once

// Prefabs (M16 part F, decision 5): a group of vobs (with children) and the way points and freepoints near
// them, kept as a template in assets/source/prefabs/<name>.g7prefab and placed as loose copies - normal vobs
// without a link to the template, like Gothic's Spacer templates. The file is a small world in the .g7world
// format (world.md), positions relative to the first vob, so no new format is needed.

#include <g7/core/Result.hpp>
#include <g7/world/Components.hpp>
#include <g7/world/WorldFile.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::world
{
class Scene;
}

namespace g7::editor
{
/// The template of `roots` (each with its children): ids from 1 (parents first), roots without parent and
/// placed relative to the first root's position (their world rotation and scale kept); the waynet entries
/// within `reach` metres (horizontally) of a vob of the template and the edges between its way points, also
/// relative. Errors: no or unknown vobs.
[[nodiscard]] Result<world::WorldFile> makePrefab(const world::Scene& scene,
                                                  std::span<const world::VobId> roots,
                                                  const world::WaynetData& waynet, std::string name,
                                                  f32 reach = 2.0f);

/// The file's text (writeWorldFile) and back (parseWorldFile).
[[nodiscard]] std::string writePrefab(const world::WorldFile& prefab);
[[nodiscard]] Result<world::WorldFile> parsePrefab(std::string_view text, std::string_view source);

struct PlacedPrefab
{
    std::vector<world::VobId> vobs;       ///< every new vob (new ids from the scene's counter)
    std::vector<std::string> waynetNames; ///< the new way points and freepoints (renamed)
    std::vector<std::pair<std::string, std::string>> waynetEdges; ///< (smaller, larger)
};

/// Copies of the template at `at`, turned by `yawDegrees` about +Y: vobs with new ids (parents kept inside
/// the template), way points and freepoints renamed to the next free number of their stem
/// ("FP_SIT_CAMPFIRE_01" -> "FP_SIT_CAMPFIRE_03"), edges between them. Nothing links back to the template.
[[nodiscard]] Result<PlacedPrefab> placePrefab(world::Scene& scene, world::WaynetData& waynet,
                                               const world::WorldFile& prefab, const Vec3& at,
                                               f32 yawDegrees = 0.0f);

/// The names that placePrefab would give the template's way points and freepoints (old -> new), in order.
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
prefabWaynetNames(const world::WaynetData& waynet, const world::WorldFile& prefab);
} // namespace g7::editor

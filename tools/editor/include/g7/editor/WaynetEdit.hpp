#pragma once

// Waynet editing (M16 part B, docs/modules/editor.md): plain functions on a world's waynet block
// (world::WaynetData, contract world.md "Wegnetz"), tested without a window. The editor runs them as undoable
// commands and hands the result to the engine (Engine::setWaynet). Contract rules kept here: a generated
// point that is moved or renamed loses its owner (it belongs to the human then); deleting a generated point
// lasts only until the next generator run - for good it belongs into welt's annotations (removalAnnotation,
// decision 2: shown, never written).

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/world/Waynet.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace g7::editor
{
/// Part of a waynet as it was: the entries named (points, freepoints), the edges named and every edge at a
/// named point. A name or edge missing from `entries` was not there.
struct WaynetSnapshot
{
    std::vector<std::string> names;
    std::vector<std::pair<std::string, std::string>> edges; ///< (smaller, larger)
    world::WaynetData entries;

    friend bool operator==(const WaynetSnapshot&, const WaynetSnapshot&) = default;
};

/// The state of the entries `names` (points or freepoints), of `edges`, and of all edges at those points.
[[nodiscard]] WaynetSnapshot captureWaynet(const world::WaynetData& waynet,
                                           std::span<const std::string> names,
                                           std::span<const std::pair<std::string, std::string>> edges = {});
/// Makes the waynet match the snapshot for what it names; the rest stays. Normalized (sorted) afterwards.
[[nodiscard]] Result<void> restoreWaynet(world::WaynetData& waynet, const WaynetSnapshot& snapshot);

/// The edge (a, b) in written order (smaller name first).
[[nodiscard]] std::pair<std::string, std::string> edgeKey(std::string_view a, std::string_view b);

/// A free name `<prefix><stem>_<NN>` (NN from 01), e.g. ("WP_", "CAMP") -> "WP_CAMP_03".
[[nodiscard]] std::string suggestName(const world::WaynetData& waynet, std::string_view prefix,
                                      std::string_view stem);
/// The stem of a name for new ones next to it: "WP_LEO_MARKT_07" -> "LEO_MARKT", "FP_SIT_CAMP_01" -> "CAMP".
[[nodiscard]] std::string nameStem(std::string_view name);

[[nodiscard]] Result<void> addPoint(world::WaynetData& waynet, std::string name, const Vec3& position);
[[nodiscard]] Result<void> addFreepoint(world::WaynetData& waynet, std::string name, const Vec3& position,
                                        const Vec3& dir);
/// Moves a point or freepoint (and sets its facing, if given). A generated one becomes the human's.
[[nodiscard]] Result<void> moveEntry(world::WaynetData& waynet, std::string_view name, const Vec3& position,
                                     std::optional<Vec3> dir = std::nullopt);
/// Renames a point or freepoint (its edges follow). A generated one becomes the human's.
[[nodiscard]] Result<void> renameEntry(world::WaynetData& waynet, std::string_view from, std::string to);
/// Removes a point (with its edges) or a freepoint.
[[nodiscard]] Result<void> removeEntry(world::WaynetData& waynet, std::string_view name);
/// Connects two points; false if they were already connected.
[[nodiscard]] Result<bool> connect(world::WaynetData& waynet, std::string_view a, std::string_view b);
/// Removes the edge between two points; false if there was none.
[[nodiscard]] Result<bool> disconnect(world::WaynetData& waynet, std::string_view a, std::string_view b);

/// The entry is generated (owner "worldgen", world.md): the next generator run replaces it.
[[nodiscard]] bool isGeneratedEntry(const world::WaynetData& waynet, std::string_view name);
/// What welt adds to tools/worldgen/data/<site>/waynet.json so a generated point stays away for good: the
/// line for its "remove" list, ready to copy (decision 2: the editor shows it, welt writes it).
[[nodiscard]] std::string removalAnnotation(std::string_view name);
} // namespace g7::editor

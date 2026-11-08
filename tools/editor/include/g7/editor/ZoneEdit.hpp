#pragma once

// Zone editing (M16 part C, docs/modules/editor.md): the world's zones block (world.md "Zonen": indoor, music
// and ambient boxes; other types only shown) as plain functions, tested without a window. The editor runs the
// edits as undoable commands on a copy and hands the result to the engine (Engine::setZones).

#include <g7/core/Math.hpp>
#include <g7/core/Transform.hpp>
#include <g7/editor/Gizmo.hpp>
#include <g7/world/WorldFile.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::editor
{
/// The zone types the editor edits (their boxes); others are shown and kept.
inline constexpr std::string_view kZoneTypes[] = {"indoor", "music", "ambient"};

/// A known type with a box.
[[nodiscard]] bool editableZone(const world::Zone& zone) noexcept;

/// The box as a transform for the gizmo: position = centre, rotation = yaw about +Y, scale = half extents.
[[nodiscard]] Transform zoneTransform(const world::ZoneBox& box) noexcept;
/// Back from a transform: only the yaw of its rotation counts (boxes turn about +Y only); half extents at
/// least 5 cm.
[[nodiscard]] world::ZoneBox zoneBoxFrom(const Transform& transform) noexcept;
/// The box's matrix without its size (translation and yaw), for drawing with the half extents.
[[nodiscard]] Mat4 zoneMatrix(const world::ZoneBox& box) noexcept;

/// Index of the smallest zone of `type` holding `point` (the rule of world.md); nullopt if none.
[[nodiscard]] std::optional<usize> smallestZoneIndex(std::span<const world::Zone> zones,
                                                     std::string_view type, const Vec3& point) noexcept;
/// The zone a click picks: the nearest box the ray enters from outside; if the eye is inside all boxes it
/// hits, the smallest of those. nullopt if it hits none.
[[nodiscard]] std::optional<usize> pickZone(std::span<const world::Zone> zones, const Ray& ray) noexcept;

/// What `value` may be for `type`: music - the themes (without "common"), ambient - the ambiences, indoor -
/// the room tags the world already has (free text allowed). Sorted.
[[nodiscard]] std::vector<std::string> zoneValues(std::string_view type, std::span<const world::Zone> zones,
                                                  std::span<const std::string> musicThemes,
                                                  std::span<const std::string> ambiences);
/// A new zone at `centre`: rooms 3 x 1.5 x 3 m (half), music and ambience 20 x 10 x 20 m.
[[nodiscard]] world::Zone newZone(std::string type, std::string value, const Vec3& centre);
/// A line for the list: "music LAGER (40 x 20 x 40 m)".
[[nodiscard]] std::string zoneLabel(const world::Zone& zone);
} // namespace g7::editor

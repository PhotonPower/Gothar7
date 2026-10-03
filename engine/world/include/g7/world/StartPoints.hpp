#pragma once

// Start points (vob type "start"): where the player - in M4 the camera - begins in a world.

#include <g7/core/Result.hpp>
#include <g7/world/Components.hpp>

#include <entt/entity/entity.hpp>

#include <string_view>

namespace g7::world
{
class Scene;

/// The start point named `name` (case-insensitive) or, with an empty name, the one with the lowest
/// id. Errors: no start point in the world, or an unknown name (the message lists the known ones).
[[nodiscard]] Result<entt::entity> findStartPoint(const Scene& scene, std::string_view name = {});
} // namespace g7::world

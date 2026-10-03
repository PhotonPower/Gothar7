#pragma once

// Editor operations on the world (M4): plain functions on world::Scene, so they are tested without
// a window. The editor calls them; afterwards Engine::refreshScene() brings rendering up to date.

#include <g7/core/Geometry.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Transform.hpp>
#include <g7/editor/Gizmo.hpp>
#include <g7/world/Components.hpp>

#include <entt/entity/entity.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace g7
{
struct SceneInstance;
}

namespace g7::world
{
class Scene;
}

namespace g7::editor
{
/// World placement of a vob (position, rotation, scale from its world matrix; no shear).
[[nodiscard]] Transform worldTransformOf(const world::Scene& scene, entt::entity vob);
/// Places the vob at `world`, keeping its parent: the local transform becomes parent^-1 * world.
void setWorldTransform(world::Scene& scene, entt::entity vob, const Transform& world);

/// A new mesh vob (deco) for the model at `meshPath`, at `position`, named after the file
/// ("models/hut_a.glb" -> "HUT_A"); its id comes from the scene's nextVobId.
[[nodiscard]] Result<entt::entity> placeMesh(world::Scene& scene, std::string_view meshPath,
                                             const Vec3& position);
/// A copy of the vob with all its components (same parent and name, a new id), moved by `offset`.
/// Children are not copied.
[[nodiscard]] Result<entt::entity> duplicateVob(world::Scene& scene, entt::entity vob, const Vec3& offset);
/// Removes the vob and its children; their ids stay used (never handed out again, ADR 0005).
void removeVob(world::Scene& scene, entt::entity vob);

/// Distance along the ray to the box, if it hits (0 when starting inside).
[[nodiscard]] std::optional<f32> intersect(const Ray& ray, const AABB& box) noexcept;
/// The vob hit first by the ray: drawn meshes by their bounds, start points, sounds, lights and
/// triggers (which draw nothing themselves) by their debug shapes. 0 = nothing.
[[nodiscard]] world::VobId pick(const world::Scene& scene, std::span<const SceneInstance> instances,
                                const Ray& ray);
} // namespace g7::editor

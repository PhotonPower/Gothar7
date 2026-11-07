#pragma once

// A .g7world into a Scene and back (world; the format itself is in world_format).

#include <g7/core/Result.hpp>
#include <g7/world/WorldFile.hpp>

#include <entt/entity/entity.hpp>

#include <string_view>

namespace g7::world
{
class Scene;

/// Creates the file's vobs in `scene` with their ids (parents before children, in any file order)
/// and raises the scene's id counter to the file's nextVobId. Fails for duplicate ids, unknown
/// parents and parent cycles; nothing is created then.
[[nodiscard]] Result<void> spawnWorld(Scene& scene, const WorldFile& world);
/// The scene's world vobs (runtime vobs are left out) as a file, e.g. for the editor.
[[nodiscard]] WorldFile captureWorld(const Scene& scene, std::string_view name);
/// One vob as the file has it (local transform, parent id, the component of its type) - the editor's undo.
[[nodiscard]] WorldFileVob captureVob(const Scene& scene, entt::entity vob);
/// Puts `vob` back as captured: an existing vob with its id gets name, parent, local transform and components
/// (its children stay), else it is spawned with its id (the parent must exist). The editor's undo and redo.
[[nodiscard]] Result<entt::entity> restoreVob(Scene& scene, const WorldFileVob& vob);
} // namespace g7::world

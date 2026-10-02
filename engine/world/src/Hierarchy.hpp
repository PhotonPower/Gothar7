#pragma once

// Internal: parent/child links and change tracking of Scene (not part of the public components).

#include <entt/entity/entity.hpp>

namespace g7::world::detail
{
/// Intrusive child list: every vob has one; children are linked through their siblings.
struct Hierarchy
{
    entt::entity parent = entt::null;
    entt::entity firstChild = entt::null;
    entt::entity nextSibling = entt::null;
    entt::entity previousSibling = entt::null;
};

/// The local transform or the parent changed: the world matrices of this subtree are stale.
struct TransformDirty
{
};
} // namespace g7::world::detail

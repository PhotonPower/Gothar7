#pragma once

// The world's entities and their hierarchy (ADR 0005, docs/modules/world.md). EnTT is a public
// dependency of world, but the registry is not part of the API: upper modules go through Scene.

#include <g7/core/Result.hpp>
#include <g7/core/Transform.hpp>
#include <g7/world/Components.hpp>

#include <entt/entity/registry.hpp>

#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace g7::world
{
/// What spawnVob creates.
struct VobDesc
{
    std::string name;
    Transform transform; ///< relative to the parent
    VobId parent;        ///< optional; must exist
    /// Fixed id (world loader: the id stored in .g7world). Must be unique and below kRuntimeVobIdBase.
    /// 0 = assign the next free one.
    VobId id;
    /// Spawned while playing (items, NPCs): the id comes from the runtime range.
    bool runtime = false;
};

/// The entities of one world. Main thread only.
class Scene
{
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /// Creates a vob with Vob, Transform and WorldTransform. Fails for a duplicate id, a fixed
    /// world id in the runtime range (or a runtime vob with a fixed id) and an unknown parent.
    [[nodiscard]] Result<entt::entity> spawnVob(const VobDesc& desc);
    /// Destroys the vob and all its descendants. Their ids are never handed out again.
    void destroyVob(entt::entity vob);
    [[nodiscard]] bool valid(entt::entity vob) const noexcept;
    [[nodiscard]] usize vobCount() const noexcept { return m_byId.size(); }

    [[nodiscard]] entt::entity findById(VobId id) const;
    /// First vob with this name (names need not be unique), or entt::null.
    [[nodiscard]] entt::entity findByName(StringId name) const;
    [[nodiscard]] VobId idOf(entt::entity vob) const;

    // --- Components ---
    template <typename C>
    C& set(entt::entity vob, C component)
    {
        if constexpr (std::is_same_v<C, Transform>)
        {
            markDirty(vob);
        }
        return m_registry.emplace_or_replace<C>(vob, std::move(component));
    }
    /// nullptr if the vob has no such component. Change a Transform through set()/setTransform(),
    /// so the world matrices follow.
    template <typename C>
    [[nodiscard]] const C* get(entt::entity vob) const
    {
        return m_registry.try_get<C>(vob);
    }
    template <typename C>
    [[nodiscard]] C* get(entt::entity vob)
    {
        static_assert(!std::is_same_v<C, Transform>, "change transforms with setTransform()");
        return m_registry.try_get<C>(vob);
    }
    template <typename C>
    [[nodiscard]] bool has(entt::entity vob) const
    {
        return m_registry.all_of<C>(vob);
    }
    template <typename C>
    void remove(entt::entity vob)
    {
        static_assert(!std::is_same_v<C, Vob> && !std::is_same_v<C, Transform> &&
                          !std::is_same_v<C, WorldTransform>,
                      "core components stay until destroyVob");
        m_registry.remove<C>(vob);
    }
    /// Calls fn(entity, C&...) for every vob that has all of C. A Transform is handed out as
    /// const (change it with setTransform(), so the world matrices follow).
    template <typename... C, typename Fn>
    void each(Fn&& fn)
    {
        auto view = m_registry.view<C...>();
        for (const entt::entity e : view)
        {
            fn(e, expose<C>(view.template get<C>(e))...);
        }
    }

    /// Read-only variant: fn(entity, const C&...).
    template <typename... C, typename Fn>
    void each(Fn&& fn) const
    {
        auto view = m_registry.view<const C...>();
        for (const entt::entity e : view)
        {
            fn(e, view.template get<const C>(e)...);
        }
    }

    // --- Hierarchy and placement ---
    void setTransform(entt::entity vob, const Transform& local) { set<Transform>(vob, local); }
    /// Re-parents `child` (entt::null = root) keeping its world placement. Fails for invalid
    /// vobs and cycles.
    [[nodiscard]] Result<void> setParent(entt::entity child, entt::entity parent);
    [[nodiscard]] entt::entity parent(entt::entity vob) const;
    [[nodiscard]] std::vector<entt::entity> children(entt::entity vob) const;
    /// World matrix computed from the local transforms now (does not need updateTransforms()).
    [[nodiscard]] Mat4 worldMatrix(entt::entity vob) const;
    /// Brings WorldTransform up to date for every vob whose own or an ancestor's transform or
    /// parent changed since the last call; untouched subtrees are skipped.
    void updateTransforms();

    // --- Id counters (ADR 0005; saved in .g7world and the save game) ---
    [[nodiscard]] u64 nextVobId() const noexcept { return m_nextVobId; }
    /// Never lowers the counter ("only rises").
    [[nodiscard]] Result<void> setNextVobId(u64 next);
    [[nodiscard]] u64 nextRuntimeVobId() const noexcept { return m_nextRuntimeVobId; }
    [[nodiscard]] Result<void> setNextRuntimeVobId(u64 next);

private:
    template <typename C>
    static decltype(auto) expose(C& component) noexcept
    {
        if constexpr (std::is_same_v<C, Transform>)
        {
            return static_cast<const C&>(component);
        }
        else
        {
            return static_cast<C&>(component);
        }
    }

    void markDirty(entt::entity vob);
    void destroyRecursive(entt::entity vob);
    void link(entt::entity child, entt::entity parent);
    void unlink(entt::entity child);

    entt::registry m_registry;
    std::unordered_map<VobId, entt::entity> m_byId;
    u64 m_nextVobId = 1;
    u64 m_nextRuntimeVobId = kRuntimeVobIdBase;
};
} // namespace g7::world

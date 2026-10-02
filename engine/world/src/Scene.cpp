#include "Hierarchy.hpp"

#include <g7/world/Scene.hpp>

#include <format>

namespace g7::world
{
using detail::Hierarchy;
using detail::TransformDirty;

Scene::Scene() = default;
Scene::~Scene() = default;

Result<entt::entity> Scene::spawnVob(const VobDesc& desc)
{
    VobId id = desc.id;
    if (desc.runtime)
    {
        if (id.valid())
        {
            return Error{"runtime vobs get their id from the runtime counter, not a fixed one"};
        }
        id = VobId{m_nextRuntimeVobId};
    }
    else if (id.valid())
    {
        // Loading: the id comes from the data, so mistakes are errors, not asserts.
        if (id.runtime())
        {
            return Error{std::format("vob id {} lies in the runtime range", id.value)};
        }
        if (m_byId.contains(id))
        {
            return Error{std::format("duplicate vob id {}", id.value)};
        }
    }
    else
    {
        id = VobId{m_nextVobId};
    }

    entt::entity parentEntity = entt::null;
    if (desc.parent.valid())
    {
        parentEntity = findById(desc.parent);
        if (parentEntity == entt::null)
        {
            return Error{std::format("parent vob {} does not exist", desc.parent.value)};
        }
    }

    // Counters only rise: never hand out an id at or below one that exists.
    if (id.runtime())
    {
        m_nextRuntimeVobId = std::max(m_nextRuntimeVobId, id.value + 1);
    }
    else
    {
        m_nextVobId = std::max(m_nextVobId, id.value + 1);
    }

    const entt::entity e = m_registry.create();
    m_registry.emplace<Vob>(e, Vob{id, desc.name});
    m_registry.emplace<Transform>(e, desc.transform);
    m_registry.emplace<WorldTransform>(e);
    m_registry.emplace<Hierarchy>(e);
    m_registry.emplace<TransformDirty>(e);
    m_byId.emplace(id, e);
    if (parentEntity != entt::null)
    {
        link(e, parentEntity);
    }
    return e;
}

void Scene::destroyVob(entt::entity vob)
{
    if (!valid(vob))
    {
        return;
    }
    unlink(vob);
    destroyRecursive(vob);
}

void Scene::destroyRecursive(entt::entity vob)
{
    entt::entity child = m_registry.get<Hierarchy>(vob).firstChild;
    while (child != entt::null)
    {
        const entt::entity next = m_registry.get<Hierarchy>(child).nextSibling;
        destroyRecursive(child);
        child = next;
    }
    m_byId.erase(m_registry.get<Vob>(vob).id);
    m_registry.destroy(vob);
}

bool Scene::valid(entt::entity vob) const noexcept
{
    return vob != entt::null && m_registry.valid(vob) && m_registry.all_of<Vob>(vob);
}

entt::entity Scene::findById(VobId id) const
{
    const auto found = m_byId.find(id);
    return found != m_byId.end() ? found->second : entt::null;
}

entt::entity Scene::findByName(StringId name) const
{
    for (const auto [e, vob] : m_registry.view<const Vob>().each())
    {
        if (vob.name == name)
        {
            return e;
        }
    }
    return entt::null;
}

VobId Scene::idOf(entt::entity vob) const
{
    const Vob* v = valid(vob) ? m_registry.try_get<Vob>(vob) : nullptr;
    return v != nullptr ? v->id : VobId{};
}

void Scene::link(entt::entity child, entt::entity parent)
{
    Hierarchy& c = m_registry.get<Hierarchy>(child);
    Hierarchy& p = m_registry.get<Hierarchy>(parent);
    c.parent = parent;
    c.previousSibling = entt::null;
    c.nextSibling = p.firstChild;
    if (p.firstChild != entt::null)
    {
        m_registry.get<Hierarchy>(p.firstChild).previousSibling = child;
    }
    p.firstChild = child;
    markDirty(child);
}

void Scene::unlink(entt::entity child)
{
    Hierarchy& c = m_registry.get<Hierarchy>(child);
    if (c.parent == entt::null)
    {
        return;
    }
    if (c.previousSibling != entt::null)
    {
        m_registry.get<Hierarchy>(c.previousSibling).nextSibling = c.nextSibling;
    }
    else
    {
        m_registry.get<Hierarchy>(c.parent).firstChild = c.nextSibling;
    }
    if (c.nextSibling != entt::null)
    {
        m_registry.get<Hierarchy>(c.nextSibling).previousSibling = c.previousSibling;
    }
    c.parent = c.nextSibling = c.previousSibling = entt::null;
    markDirty(child);
}

Result<void> Scene::setParent(entt::entity child, entt::entity parent)
{
    if (!valid(child) || (parent != entt::null && !valid(parent)))
    {
        return Error{"setParent: invalid vob"};
    }
    // A vob cannot hang below itself or one of its descendants.
    for (entt::entity a = parent; a != entt::null; a = m_registry.get<Hierarchy>(a).parent)
    {
        if (a == child)
        {
            return Error{std::format("setParent: vob {} would become its own ancestor", idOf(child).value)};
        }
    }
    const Mat4 world = worldMatrix(child);
    unlink(child);
    if (parent != entt::null)
    {
        link(child, parent);
    }
    // Keep the world placement: new local = inverse(new parent world) * world.
    const Mat4 parentWorld = parent != entt::null ? worldMatrix(parent) : Mat4(1.0f);
    set<Transform>(child, Transform::fromMatrix(glm::inverse(parentWorld) * world));
    return {};
}

entt::entity Scene::parent(entt::entity vob) const
{
    return valid(vob) ? m_registry.get<Hierarchy>(vob).parent : entt::null;
}

std::vector<entt::entity> Scene::children(entt::entity vob) const
{
    std::vector<entt::entity> result;
    if (!valid(vob))
    {
        return result;
    }
    for (entt::entity c = m_registry.get<Hierarchy>(vob).firstChild; c != entt::null;
         c = m_registry.get<Hierarchy>(c).nextSibling)
    {
        result.push_back(c);
    }
    return result;
}

Mat4 Scene::worldMatrix(entt::entity vob) const
{
    Mat4 matrix(1.0f);
    for (entt::entity e = vob; valid(e); e = m_registry.get<Hierarchy>(e).parent)
    {
        matrix = m_registry.get<Transform>(e).toMatrix() * matrix;
    }
    return matrix;
}

void Scene::markDirty(entt::entity vob)
{
    m_registry.emplace_or_replace<TransformDirty>(vob);
}

void Scene::updateTransforms()
{
    // Start at the topmost dirty vob of each changed subtree and recompute everything below it;
    // the parent's world matrix is up to date (it is clean or was handled first).
    std::vector<entt::entity> roots;
    for (const entt::entity e : m_registry.view<TransformDirty>())
    {
        bool ancestorDirty = false;
        for (entt::entity a = m_registry.get<Hierarchy>(e).parent; a != entt::null && !ancestorDirty;
             a = m_registry.get<Hierarchy>(a).parent)
        {
            ancestorDirty = m_registry.all_of<TransformDirty>(a);
        }
        if (!ancestorDirty)
        {
            roots.push_back(e);
        }
    }
    std::vector<entt::entity> stack;
    for (const entt::entity root : roots)
    {
        stack.push_back(root);
        while (!stack.empty())
        {
            const entt::entity e = stack.back();
            stack.pop_back();
            const Hierarchy& h = m_registry.get<Hierarchy>(e);
            const Mat4 parentWorld =
                h.parent != entt::null ? m_registry.get<WorldTransform>(h.parent).matrix : Mat4(1.0f);
            m_registry.get<WorldTransform>(e).matrix = parentWorld * m_registry.get<Transform>(e).toMatrix();
            for (entt::entity c = h.firstChild; c != entt::null; c = m_registry.get<Hierarchy>(c).nextSibling)
            {
                stack.push_back(c);
            }
        }
    }
    m_registry.clear<TransformDirty>();
}

Result<void> Scene::setNextVobId(u64 next)
{
    if (next < m_nextVobId)
    {
        return Error{std::format("nextVobId only rises: {} is below {}", next, m_nextVobId)};
    }
    if (next > kRuntimeVobIdBase)
    {
        return Error{std::format("nextVobId {} reaches into the runtime range", next)};
    }
    m_nextVobId = next;
    return {};
}

Result<void> Scene::setNextRuntimeVobId(u64 next)
{
    if (next < m_nextRuntimeVobId)
    {
        return Error{std::format("nextRuntimeVobId only rises: {} is below {}", next, m_nextRuntimeVobId)};
    }
    m_nextRuntimeVobId = next;
    return {};
}
} // namespace g7::world

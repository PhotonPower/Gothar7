#include <g7/world/Scene.hpp>
#include <g7/world/WorldScene.hpp>

#include <algorithm>
#include <format>
#include <unordered_map>

namespace g7::world
{
namespace
{
/// The vob's components as the file has them (the type's component, the footstep surface); the others
/// removed.
void applyComponents(Scene& scene, entt::entity e, const WorldFileVob& vob)
{
    scene.remove<MeshRef>(e);
    scene.remove<LightSource>(e);
    scene.remove<StartPoint>(e);
    scene.remove<SoundEmitter>(e);
    scene.remove<TriggerVolume>(e);
    scene.remove<MobRef>(e);
    scene.remove<WaterVolume>(e);
    scene.remove<ItemRef>(e);
    scene.remove<SurfaceRef>(e);
    switch (vob.type)
    {
    case VobType::Empty:
        break;
    case VobType::Mesh:
        scene.set<MeshRef>(e, {vob.mesh, vob.category});
        break;
    case VobType::Light:
        scene.set<LightSource>(e, vob.light);
        break;
    case VobType::Start:
        scene.set<StartPoint>(e, {});
        break;
    case VobType::Sound:
        scene.set<SoundEmitter>(e, vob.sound);
        break;
    case VobType::Trigger:
        scene.set<TriggerVolume>(e, vob.trigger);
        break;
    case VobType::Mob:
        scene.set<MeshRef>(e, {vob.mesh, VobCategory::Gameplay});
        scene.set<MobRef>(e, vob.mob);
        break;
    case VobType::Water:
        scene.set<WaterVolume>(e, vob.water);
        break;
    case VobType::Item:
        scene.set<ItemRef>(e, vob.item);
        break;
    }
    if (!vob.footstep.empty())
    {
        scene.set<SurfaceRef>(e, {vob.footstep}); // M13 E: the footstep material
    }
}
} // namespace

Result<void> spawnWorld(Scene& scene, const WorldFile& world)
{
    // Check everything first, so a broken file leaves the scene untouched.
    std::unordered_map<u64, const WorldFileVob*> byId;
    for (const WorldFileVob& vob : world.vobs)
    {
        if (!byId.emplace(vob.id.value, &vob).second || scene.findById(vob.id) != entt::null)
        {
            return Error{std::format("{}: duplicate vob id {}", world.name, vob.id.value)};
        }
        if (vob.id.runtime())
        {
            return Error{std::format("{}: vob id {} lies in the runtime range", world.name, vob.id.value)};
        }
    }
    // Parents before children, whatever the file order: depth = length of the parent chain.
    std::unordered_map<u64, usize> depth;
    for (const WorldFileVob& vob : world.vobs)
    {
        usize d = 0;
        for (VobId p = vob.parent; p.valid(); ++d)
        {
            const auto parent = byId.find(p.value);
            if (parent == byId.end())
            {
                return Error{
                    std::format("{}: vob {} has unknown parent {}", world.name, vob.id.value, p.value)};
            }
            if (d > world.vobs.size())
            {
                return Error{std::format("{}: parent cycle at vob {}", world.name, vob.id.value)};
            }
            p = parent->second->parent;
        }
        depth[vob.id.value] = d;
    }
    std::vector<const WorldFileVob*> order;
    for (const WorldFileVob& vob : world.vobs)
    {
        order.push_back(&vob);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](const auto* a, const auto* b) { return depth[a->id.value] < depth[b->id.value]; });

    for (const WorldFileVob* vob : order)
    {
        auto e = scene.spawnVob({vob->name, vob->transform, vob->parent, vob->id});
        if (!e)
        {
            return e.error(); // not expected after the checks above
        }
        applyComponents(scene, e.value(), *vob);
    }
    if (world.nextVobId > scene.nextVobId())
    {
        if (auto raised = scene.setNextVobId(world.nextVobId); !raised)
        {
            return raised;
        }
    }
    return {};
}

WorldFileVob captureVob(const Scene& scene, entt::entity e)
{
    WorldFileVob out;
    const Vob* vob = scene.get<Vob>(e);
    const Transform* transform = scene.get<Transform>(e);
    if (vob == nullptr || transform == nullptr)
    {
        return out;
    }
    out.id = vob->id;
    out.name = vob->nameText;
    out.parent = scene.idOf(scene.parent(e));
    out.transform = *transform;
    if (const ItemRef* item = scene.get<ItemRef>(e))
    {
        out.type = VobType::Item;
        out.item = *item;
    }
    else if (const MobRef* mob = scene.get<MobRef>(e))
    {
        out.type = VobType::Mob;
        out.mob = *mob;
        const MeshRef* mesh = scene.get<MeshRef>(e);
        out.mesh = mesh != nullptr ? mesh->path : std::string();
    }
    else if (const MeshRef* mesh = scene.get<MeshRef>(e))
    {
        out.type = VobType::Mesh;
        out.mesh = mesh->path;
        out.category = mesh->category;
    }
    else if (const LightSource* light = scene.get<LightSource>(e))
    {
        out.type = VobType::Light;
        out.light = *light;
    }
    else if (scene.has<StartPoint>(e))
    {
        out.type = VobType::Start;
    }
    else if (const SoundEmitter* sound = scene.get<SoundEmitter>(e))
    {
        out.type = VobType::Sound;
        out.sound = *sound;
    }
    else if (const TriggerVolume* trigger = scene.get<TriggerVolume>(e))
    {
        out.type = VobType::Trigger;
        out.trigger = *trigger;
    }
    else if (const WaterVolume* water = scene.get<WaterVolume>(e))
    {
        out.type = VobType::Water;
        out.water = *water;
    }
    if (const SurfaceRef* surface = scene.get<SurfaceRef>(e);
        surface != nullptr && (out.type == VobType::Mesh || out.type == VobType::Mob))
    {
        out.footstep = surface->footstep;
    }
    return out;
}

Result<entt::entity> restoreVob(Scene& scene, const WorldFileVob& vob)
{
    const entt::entity parent = vob.parent.valid() ? scene.findById(vob.parent) : entt::null;
    if (vob.parent.valid() && parent == entt::null)
    {
        return Error{std::format("restoreVob: vob {} has unknown parent {}", vob.id.value, vob.parent.value)};
    }
    entt::entity e = scene.findById(vob.id);
    if (e == entt::null)
    {
        auto spawned = scene.spawnVob({vob.name, vob.transform, vob.parent, vob.id});
        if (!spawned)
        {
            return spawned.error();
        }
        e = spawned.value();
    }
    else
    {
        if (scene.parent(e) != parent)
        {
            if (auto moved = scene.setParent(e, parent); !moved)
            {
                return moved.error();
            }
        }
        Vob v = *scene.get<Vob>(e);
        v.nameText = vob.name;
        v.name = StringId(vob.name);
        scene.set<Vob>(e, v);
        scene.setTransform(e, vob.transform);
    }
    applyComponents(scene, e, vob);
    return e;
}

WorldFile captureWorld(const Scene& scene, std::string_view name)
{
    WorldFile world;
    world.name = std::string(name);
    world.nextVobId = scene.nextVobId();
    scene.each<Vob, Transform>(
        [&](entt::entity e, const Vob& vob, const Transform&)
        {
            if (!vob.id.runtime())
            {
                world.vobs.push_back(captureVob(scene, e));
            }
        });
    return world;
}
} // namespace g7::world

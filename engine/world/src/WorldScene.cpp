#include <g7/world/Scene.hpp>
#include <g7/world/WorldScene.hpp>

#include <algorithm>
#include <format>
#include <unordered_map>

namespace g7::world
{
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
        switch (vob->type)
        {
        case VobType::Empty:
            break;
        case VobType::Mesh:
            scene.set<MeshRef>(e.value(), {vob->mesh, vob->category});
            break;
        case VobType::Light:
            scene.set<LightSource>(e.value(), vob->light);
            break;
        case VobType::Start:
            scene.set<StartPoint>(e.value(), {});
            break;
        case VobType::Sound:
            scene.set<SoundEmitter>(e.value(), vob->sound);
            break;
        case VobType::Trigger:
            scene.set<TriggerVolume>(e.value(), vob->trigger);
            break;
        case VobType::Mob:
            scene.set<MeshRef>(e.value(), {vob->mesh, VobCategory::Gameplay});
            scene.set<MobRef>(e.value(), vob->mob);
            break;
        case VobType::Water:
            scene.set<WaterVolume>(e.value(), vob->water);
            break;
        case VobType::Item:
            scene.set<ItemRef>(e.value(), vob->item);
            break;
        }
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

WorldFile captureWorld(const Scene& scene, std::string_view name)
{
    WorldFile world;
    world.name = std::string(name);
    world.nextVobId = scene.nextVobId();
    scene.each<Vob, Transform>(
        [&](entt::entity e, const Vob& vob, const Transform& transform)
        {
            if (vob.id.runtime())
            {
                return;
            }
            WorldFileVob out;
            out.id = vob.id;
            out.name = vob.nameText;
            out.parent = scene.idOf(scene.parent(e));
            out.transform = transform;
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
            world.vobs.push_back(std::move(out));
        });
    return world;
}
} // namespace g7::world

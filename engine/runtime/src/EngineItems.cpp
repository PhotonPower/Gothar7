// Items in the world, focus and picking up (M8 part B): item vobs (`ItemRef`, from .g7world or insert/drop)
// drawn with their model and no collision; the focus (gameplay::selectFocus) over items, mobs and NPCs with
// its name above the target; the action key picks up the focused item (with none/t_pickup_ground once the
// graph has a "pickup" state); the inventory window (Tab) until the real screen (M13).

#include "PlayerFigure.hpp"

#include <g7/asset/Procedural.hpp>
#include <g7/core/Log.hpp>
#include <g7/gameplay/Character.hpp>
#include <g7/gameplay/Movement.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <format>

namespace g7
{
namespace
{
constexpr f32 kEyeHeight = 1.6f;       ///< focus is measured from here above the hero's feet
constexpr f32 kPickupFallback = 0.35f; ///< seconds into the pickup without an event: the item is taken
constexpr f32 kPickupTimeout = 3.0f;   ///< a pickup animation that never ends releases the hero anyway
/// Creature ids in the focus share one number space with vob ids: bit 62 is set in neither world ids (small)
/// nor runtime ids (kRuntimeVobIdBase = bit 63 plus a counter).
constexpr u64 kCreatureFocusBit = 1ull << 62;

/// A placeholder model for an item without a mesh: a long bar for weapons, a small box for the rest.
asset::MeshData itemPlaceholder(std::string_view category)
{
    const bool weapon = category.starts_with("melee") || category == "bow" || category == "crossbow";
    const Vec4 colour = weapon                 ? Vec4(0.55f, 0.55f, 0.6f, 1.0f)
                        : category == "food"   ? Vec4(0.7f, 0.25f, 0.15f, 1.0f)
                        : category == "potion" ? Vec4(0.3f, 0.35f, 0.8f, 1.0f)
                                               : Vec4(0.6f, 0.45f, 0.25f, 1.0f);
    const Vec3 half = weapon ? Vec3(0.45f, 0.025f, 0.05f) : Vec3(0.08f, 0.08f, 0.08f);
    asset::MeshData mesh = asset::makeBox(half, colour);
    for (asset::Vertex& v : mesh.vertices)
    {
        v.position.y += half.y; // lying on the ground
    }
    mesh.bounds = AABB{mesh.bounds.min + Vec3(0, half.y, 0), mesh.bounds.max + Vec3(0, half.y, 0)};
    return mesh;
}

/// How an item model lies on the ground. Item models stand with their grip at the origin and the grip axis +Y
/// (characters-pipeline.md "Gegenstände"); on the ground an item rests on its broad side: its thinnest axis
/// points up (a sword, a key, a loaf lie flat). Potions stand (bottles), placeholders already lie. The lowest
/// point touches the ground.
Mat4 restingPose(const AABB& bounds, bool placeholder, std::string_view category)
{
    const Vec3 size = bounds.max - bounds.min;
    Mat4 turn(1.0f);
    if (!placeholder && category != "potion")
    {
        if (size.x < size.y && size.x <= size.z)
        {
            turn = glm::rotate(Mat4(1.0f), 1.5707963f, Vec3(0.0f, 0.0f, 1.0f)); // +X up
        }
        else if (size.z < size.y && size.z < size.x)
        {
            turn = glm::rotate(Mat4(1.0f), -1.5707963f, Vec3(1.0f, 0.0f, 0.0f)); // +Z up
        }
    }
    f32 lowest = 1e30f;
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 corner((i & 1) ? bounds.max.x : bounds.min.x, (i & 2) ? bounds.max.y : bounds.min.y,
                          (i & 4) ? bounds.max.z : bounds.min.z);
        lowest = std::min(lowest, Vec3(turn * Vec4(corner, 1.0f)).y);
    }
    return glm::translate(Mat4(1.0f), Vec3(0.0f, -lowest, 0.0f)) * turn;
}

AABB transformBounds(const AABB& box, const Mat4& m)
{
    AABB out{Vec3(1e30f), Vec3(-1e30f)};
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 corner((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                          (i & 4) ? box.max.z : box.min.z);
        const Vec3 p = Vec3(m * Vec4(corner, 1.0f));
        out.min = glm::min(out.min, p);
        out.max = glm::max(out.max, p);
    }
    return out;
}
} // namespace

void Engine::loadFocusSettings()
{
    const std::string path = m_config.settings.get<std::string>("game.focus", "data/focus.toml");
    auto bytes = m_vfs.read(path);
    if (!bytes)
    {
        G7_LOG_WARN("engine", "{}: {} (focus uses its defaults)", path, bytes.error().message);
        return;
    }
    auto settings = gameplay::FocusSettings::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()), path);
    if (!settings)
    {
        G7_LOG_WARN("engine", "{} (focus uses its defaults)", settings.error().message);
        return;
    }
    m_focusSettings = settings.value();
}

const LoadedModel* Engine::itemModel(std::string_view instance)
{
    if (const auto it = m_itemModels.find(std::string(instance)); it != m_itemModels.end())
    {
        return it->second;
    }
    const script::Instance* item = m_scripts ? m_scripts->findInstance("Item", instance) : nullptr;
    const std::string mesh = item != nullptr ? std::string(item->fields["mesh"].asString()) : std::string();
    const LoadedModel* model = nullptr;
    if (!mesh.empty() && m_vfs.exists(mesh) && loadModels({mesh}))
    {
        model = this->model(mesh);
    }
    if (model == nullptr)
    {
        // One placeholder per category, shared by all items of it.
        const std::string category = item != nullptr && item->fields["category"].isString()
                                         ? std::string(item->fields["category"].asString())
                                         : std::string("misc");
        const std::string key = "placeholder " + category;
        if (const auto it = m_itemModels.find(key); it != m_itemModels.end())
        {
            model = it->second;
        }
        else
        {
            auto owned = std::make_unique<LoadedModel>();
            const asset::MeshData placeholder = itemPlaceholder(category);
            owned->name = key;
            owned->bounds = placeholder.bounds;
            if (m_device)
            {
                auto gpuMesh = render::Mesh::create(*m_device, *m_geometry, placeholder);
                auto materials = render::MaterialSet::create(
                    *m_device, placeholder, render::MaterialSet::ImageLookup{}, m_meshRenderer.defaults());
                if (!gpuMesh || !materials)
                {
                    G7_LOG_WARN("engine", "cannot upload the item placeholder for {}", category);
                    return nullptr;
                }
                owned->mesh = std::move(gpuMesh).value();
                owned->materials = std::move(materials).value();
            }
            model = owned.get();
            m_itemModels.emplace(key, model);
            m_scriptModels.push_back(std::move(owned));
        }
    }
    m_itemModels.emplace(std::string(instance), model);
    const std::string_view category =
        item != nullptr && item->fields["category"].isString() ? item->fields["category"].asString() : "misc";
    m_itemRest[std::string(instance)] =
        restingPose(model->bounds, model->name.starts_with("placeholder "), category);
    return model;
}

void Engine::rebuildWorldItems()
{
    m_worldItems.clear();
    m_scene.updateTransforms();
    m_scene.each<world::Vob, world::ItemRef, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::ItemRef& item, const world::WorldTransform& t)
        {
            if (m_scripts && m_scripts->findInstance("Item", item.instance) == nullptr)
            {
                G7_LOG_WARN("engine", "item vob {} ({}): unknown Item \"{}\"", vob.id.value, vob.nameText,
                            item.instance);
            }
            const LoadedModel* model = itemModel(item.instance);
            if (model != nullptr)
            {
                const Mat4 matrix = t.matrix * m_itemRest[item.instance];
                m_worldItems.push_back({vob.id, model, matrix, transformBounds(model->bounds, matrix)});
            }
        });
}

Result<world::VobId> Engine::spawnItem(std::string_view instance, u32 count, const Vec3& at, f32 yaw)
{
    if (!m_scripts || m_scripts->findInstance("Item", instance) == nullptr)
    {
        return Error{std::format("unknown item \"{}\"", instance)};
    }
    const LoadedModel* model = itemModel(instance);
    if (model == nullptr)
    {
        return Error{std::format("no model for item \"{}\"", instance)};
    }
    world::VobDesc desc;
    desc.name = std::string(instance);
    desc.transform.position = at;
    desc.transform.rotation = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
    desc.runtime = true;
    auto vob = m_scene.spawnVob(desc);
    if (!vob)
    {
        return vob.error();
    }
    m_scene.set<world::ItemRef>(vob.value(), {std::string(instance), std::max(count, 1u)});
    m_scene.updateTransforms();
    const Mat4 matrix = m_scene.worldMatrix(vob.value()) * m_itemRest[std::string(instance)];
    const world::VobId id = m_scene.idOf(vob.value());
    m_worldItems.push_back({id, model, matrix, transformBounds(model->bounds, matrix)});
    return id;
}

void Engine::removeWorldItem(world::VobId id)
{
    std::erase_if(m_worldItems, [&](const WorldItem& item) { return item.vob == id; });
    if (const entt::entity e = m_scene.findById(id); e != entt::null)
    {
        m_scene.destroyVob(e);
    }
    if (m_focus && m_focus->id == id.value)
    {
        m_focus.reset();
    }
}

std::vector<WorldItemInfo> Engine::worldItems() const
{
    std::vector<WorldItemInfo> out;
    for (const WorldItem& item : m_worldItems)
    {
        const entt::entity e = m_scene.findById(item.vob);
        const world::ItemRef* ref = e != entt::null ? m_scene.get<world::ItemRef>(e) : nullptr;
        if (ref != nullptr)
        {
            out.push_back({item.vob, ref->instance, ref->count, Vec3(item.transform[3])});
        }
    }
    return out;
}

void Engine::appendItemDraws(const Frustum& volume, bool shadow, u32 cascade)
{
    for (const WorldItem& item : m_worldItems)
    {
        if (!volume.intersects(item.bounds) ||
            render::cullByDistance(item.bounds, m_camera.transform.position, m_cullSettings, false) !=
                render::CullResult::Kept)
        {
            continue;
        }
        if (m_multiDraw)
        {
            m_drawItems.push_back({&item.model->mesh, &item.model->materials, item.transform, item.bounds});
        }
        else if (shadow)
        {
            m_meshRenderer.drawShadow(*m_device, item.model->mesh, item.model->materials, item.transform,
                                      m_cascades[cascade]);
        }
        else
        {
            m_meshRenderer.draw(*m_device, item.model->mesh, item.model->materials, item.transform, m_camera);
        }
    }
}

std::string Engine::focusName(gameplay::FocusKind kind, u64 id) const
{
    switch (kind)
    {
    case gameplay::FocusKind::Item:
    {
        const entt::entity e = m_scene.findById(world::VobId{id});
        const world::ItemRef* ref = e != entt::null ? m_scene.get<world::ItemRef>(e) : nullptr;
        if (ref == nullptr)
        {
            return {};
        }
        const script::Instance* item = m_scripts ? m_scripts->findInstance("Item", ref->instance) : nullptr;
        const std::string name =
            item != nullptr ? std::string(item->fields["name"].asString()) : ref->instance;
        return ref->count > 1 ? std::format("{} ({})", name, ref->count) : name;
    }
    case gameplay::FocusKind::Mob:
    {
        // The name of its Mob definition (part C), else the definition's id.
        if (const auto it = m_mobs.find(id); it != m_mobs.end())
        {
            return it->second.name;
        }
        const entt::entity e = m_scene.findById(world::VobId{id});
        const world::MobRef* mob = e != entt::null ? m_scene.get<world::MobRef>(e) : nullptr;
        return mob != nullptr ? mob->definition : std::string();
    }
    case gameplay::FocusKind::Npc:
    case gameplay::FocusKind::Count:
        break;
    }
    for (const auto& c : m_creatures)
    {
        if ((c->id | kCreatureFocusBit) == id)
        {
            const script::Instance* npc = m_scripts ? m_scripts->findInstance("Npc", c->species) : nullptr;
            return npc != nullptr ? std::string(npc->fields["name"].asString()) : c->species;
        }
    }
    return {};
}

void Engine::updateFocus()
{
    if (!m_player.valid() || m_flyMode || m_pickup || m_mobUse)
    {
        if (!m_pickup && !m_mobUse)
        {
            m_focus.reset();
        }
        return; // busy: the focus stays on what is being used
    }
    const Vec3 eye = m_playerFeet + Vec3(0.0f, kEyeHeight, 0.0f);
    const Vec3 ahead = gameplay::forwardOf(m_movement.yaw());
    m_focusCandidates.clear();
    for (const WorldItem& item : m_worldItems)
    {
        m_focusCandidates.push_back(
            {item.vob.value, gameplay::FocusKind::Item, 0.5f * (item.bounds.min + item.bounds.max)});
    }
    m_scene.each<world::Vob, world::MobRef, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::MobRef&, const world::WorldTransform& t)
        {
            // The middle of what is drawn (a door's origin is its hinge), else 1 m above the origin.
            Vec3 point = Vec3(t.matrix[3]) + Vec3(0, 1, 0);
            for (const SceneInstance& instance : m_instances)
            {
                if (instance.vob == vob.id)
                {
                    point = 0.5f * (instance.bounds.min + instance.bounds.max);
                    break;
                }
            }
            m_focusCandidates.push_back({vob.id.value, gameplay::FocusKind::Mob, point});
        });
    for (const auto& c : m_creatures)
    {
        if (c->vanished)
        {
            continue; // a summon that went (M12)
        }
        // The knocked out and the dead can be looted (M11, K7): focused where they lie.
        const bool lying = c->fighter.state() == gameplay::FightState::Down ||
                           c->fighter.state() == gameplay::FightState::Dead;
        if (!c->dead || c->character)
        {
            m_focusCandidates.push_back({c->id | kCreatureFocusBit, gameplay::FocusKind::Npc,
                                         c->position + Vec3(0.0f, lying ? 0.4f : 1.2f, 0.0f)});
        }
    }
    const auto visible = [&](const gameplay::FocusCandidate& c)
    {
        if (!m_physics.valid())
        {
            return true;
        }
        const Vec3 to = c.point - eye;
        const f32 distance = glm::length(to);
        if (distance < 0.3f)
        {
            return true;
        }
        const auto hit =
            m_physics.raycast(eye, to / distance, distance, physics::layerBit(physics::Layer::World));
        // What it lies on or in does not hide it, nor does the target's own collision (a mob, a door).
        return !hit || hit->distance >= distance - 0.3f || hit->userData == c.id;
    };
    const std::optional<u64> current = m_focus ? std::optional<u64>(m_focus->id) : std::nullopt;
    const std::optional<u64> chosen =
        gameplay::selectFocus(m_focusCandidates, eye, ahead, m_focusSettings, current, visible);
    if (!chosen)
    {
        m_focus.reset();
        return;
    }
    const auto it = std::find_if(m_focusCandidates.begin(), m_focusCandidates.end(),
                                 [&](const gameplay::FocusCandidate& c) { return c.id == *chosen; });
    m_focus = FocusTarget{*chosen, it->kind, focusName(it->kind, *chosen), it->point};
}

std::optional<FocusInfo> Engine::focus() const
{
    if (!m_focus)
    {
        return std::nullopt;
    }
    return FocusInfo{m_focus->kind, m_focus->id & ~kCreatureFocusBit, m_focus->name};
}

Result<void> Engine::pickUpFocus()
{
    if (m_pickup)
    {
        return Error{"already picking something up"};
    }
    if (!m_focus || m_focus->kind != gameplay::FocusKind::Item)
    {
        return Error{"no item in focus"};
    }
    if (!m_hero)
    {
        return Error{"no hero (Npc \"pc_hero\" missing)"};
    }
    // With a "pickup" state in the graph the figure bends down and the item goes at its "pickup" event;
    // without one it goes after a moment.
    m_pickup = PendingPickup{world::VobId{m_focus->id}};
    m_pickup->animated = m_figure && m_figure->animator.hasState("pickup");
    m_pickupEvent = false;
    return {};
}

void Engine::takeItem(world::VobId id)
{
    const entt::entity e = m_scene.findById(id);
    const world::ItemRef* ref = e != entt::null ? m_scene.get<world::ItemRef>(e) : nullptr;
    if (ref == nullptr || !m_hero)
    {
        return; // gone meanwhile (a script removed it)
    }
    const std::string instance = ref->instance;
    const u32 count = ref->count;
    const std::string owner = ref->owner;
    m_hero->addItem(instance, count);
    removeWorldItem(id);
    G7_LOG_INFO("engine", "picked up {} x {}", count, instance);
    if (m_scripts)
    {
        const script::Value args[] = {instance, static_cast<i64>(count)};
        m_scripts->emit("item_taken", args);
        if (!owner.empty())
        {
            const script::Value theft[] = {owner, instance, static_cast<i64>(count)};
            m_scripts->emit("theft", theft);
            witnessed("assess_theft", theft); // M9 part C
        }
    }
}

void Engine::fixedUpdateInteraction(f32 seconds)
{
    if (!m_pickup)
    {
        return;
    }
    PendingPickup& p = *m_pickup;
    p.time += seconds;
    if (!p.taken && (m_pickupEvent || (!p.animated && p.time >= kPickupFallback) ||
                     (p.animated && p.time >= 0.5f * kPickupTimeout)))
    {
        takeItem(p.vob);
        p.taken = true;
    }
    const bool animationDone =
        !p.animated || (m_figure && m_figure->animator.state() != "pickup" && p.time > 0.1f);
    if (p.taken && (animationDone || p.time >= kPickupTimeout))
    {
        m_pickup.reset();
    }
}

Result<void> Engine::dropItem(std::string_view instance, u32 count)
{
    if (!m_hero || m_hero->itemCount(instance) < count || count == 0)
    {
        return Error{std::format("the hero has no {} x \"{}\"", count, instance)};
    }
    const Vec3 ahead = gameplay::forwardOf(m_movement.yaw());
    Vec3 at = m_playerFeet + ahead * 0.8f;
    if (m_physics.valid())
    {
        if (const auto hit = m_physics.raycast(at + Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 5.0f,
                                               physics::layerBit(physics::Layer::World)))
        {
            at.y = hit->position.y;
        }
    }
    auto spawned = spawnItem(instance, count, at, m_movement.yaw());
    if (!spawned)
    {
        return spawned.error();
    }
    (void)m_hero->removeItem(instance, count);
    return {};
}

void Engine::setInventoryOpen(bool open) noexcept
{
    m_inventoryOpen = open && m_hero != nullptr && !m_transform; // Z7: an animal carries nothing
    m_inventoryMessage.clear();
}

void Engine::inventoryUi()
{
    if (!m_inventoryOpen || !m_hero)
    {
        return;
    }
    gameplay::Character& hero = *m_hero;
    const gameplay::ItemLookup items = itemLookup();
    ui::InventoryPanel panel;
    panel.title = std::format("{} - Stufe {} ({} / {} EP, {} LP)", hero.name(), hero.level(),
                              hero.experience(), xpForLevel(hero.level() + 1), hero.learnPoints());
    panel.stats.push_back(std::format(
        "Leben {}/{}  Mana {}/{}  Stärke {}  Geschick {}", hero.attribute("hp"), hero.attribute("hp_max"),
        hero.attribute("mana"), hero.attribute("mana_max"), hero.attribute("str"), hero.attribute("dex")));
    panel.stats.push_back(std::format("Schutz: Waffen {}  Pfeile {}  Feuer {}  Magie {}",
                                      hero.protection("edge"), hero.protection("point"),
                                      hero.protection("fire"), hero.protection("magic")));
    for (const gameplay::ItemStack& stack : hero.inventory(items))
    {
        const auto info = items(stack.item);
        ui::InventoryPanel::Row row;
        row.item = stack.item;
        row.name = info ? info->name : stack.item;
        row.category = info ? info->category : "misc";
        row.count = stack.count;
        row.equippable = gameplay::Character::slotFor(row.category).has_value();
        row.usable = row.category == "food" || row.category == "potion" || row.category == "document";
        for (usize s = 0; s < static_cast<usize>(gameplay::EquipSlot::Count); ++s)
        {
            if (hero.equipped(static_cast<gameplay::EquipSlot>(s)) == stack.item)
            {
                row.equipped = std::string(gameplay::slotName(static_cast<gameplay::EquipSlot>(s)));
                break;
            }
        }
        panel.rows.push_back(std::move(row));
    }
    panel.message = m_inventoryMessage;
    const MobRuntime* chest =
        m_mobUse && m_mobUse->containerOpen ? &m_mobs.at(m_mobUse->vob.value) : nullptr; // M8 part C
    const Creature* looted = m_lootTarget ? creature(*m_lootTarget) : nullptr;           // M11 part C
    if (looted != nullptr && looted->character)
    {
        panel.container = true;
        panel.containerTitle =
            std::format("{} ({})", looted->character->name(),
                        looted->fighter.state() == gameplay::FightState::Dead ? "tot" : "bewusstlos");
        for (const gameplay::ItemStack& stack : looted->character->inventory(items))
        {
            const auto info = items(stack.item);
            panel.containerRows.push_back({stack.item,
                                           info ? info->name : stack.item,
                                           info ? info->category : "misc",
                                           stack.count,
                                           {},
                                           false});
        }
    }
    else if (chest != nullptr)
    {
        panel.container = true;
        panel.containerTitle = chest->name;
        for (const auto& [item, count] : chest->contents)
        {
            const auto info = items(item);
            panel.containerRows.push_back(
                {item, info ? info->name : item, info ? info->category : "misc", count, {}, false});
        }
    }
    m_debugUi.inventoryPanel(panel);
    if (!panel.open)
    {
        m_inventoryOpen = false;
        m_lootTarget.reset();
    }
    if (looted != nullptr && (panel.action == "take" || panel.action == "put"))
    {
        auto taken = panel.action == "take" ? loot(looted->species, panel.actionItem, 0)
                                            : Result<u32>(Error{"Hier wird nur genommen."});
        m_inventoryMessage = taken ? std::string() : taken.error().message;
        return;
    }
    if (panel.action == "equip")
    {
        auto slot = hero.equip(panel.actionItem, items);
        m_inventoryMessage = slot ? std::string() : slot.error().message;
    }
    else if (panel.action == "unequip")
    {
        for (usize s = 0; s < static_cast<usize>(gameplay::EquipSlot::Count); ++s)
        {
            if (hero.equipped(static_cast<gameplay::EquipSlot>(s)) == panel.actionItem)
            {
                hero.unequip(static_cast<gameplay::EquipSlot>(s));
                break;
            }
        }
        m_inventoryMessage.clear();
    }
    else if (panel.action == "use")
    {
        auto used = useItem(panel.actionItem);
        m_inventoryMessage = used ? std::string() : used.error().message;
    }
    else if (panel.action == "drop")
    {
        auto dropped = dropItem(panel.actionItem, 1);
        m_inventoryMessage = dropped ? std::string() : dropped.error().message;
    }
    else if ((panel.action == "take" || panel.action == "put") && m_mobUse)
    {
        const world::VobId vob = m_mobUse->vob;
        const u32 count = panel.action == "take" ? m_mobs.at(vob.value).contents[panel.actionItem]
                                                 : hero.itemCount(panel.actionItem);
        auto moved = panel.action == "take" ? takeFromMob(vob, panel.actionItem, std::max(count, 1u))
                                            : putIntoMob(vob, panel.actionItem, 1);
        m_inventoryMessage = moved ? std::string() : moved.error().message;
    }
}

void Engine::focusUi()
{
    if (!m_focus || m_focus->name.empty() || !m_window || m_inventoryOpen || m_dialog)
    {
        return;
    }
    // Above the target: its world point projected to window coordinates.
    const Vec4 clip = m_camera.viewProjection() * Vec4(m_focus->point + Vec3(0.0f, 0.35f, 0.0f), 1.0f);
    if (clip.w <= 0.0f)
    {
        return;
    }
    const Vec2 ndc = Vec2(clip) / clip.w;
    if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f)
    {
        return;
    }
    const auto size = m_window->size();
    m_debugUi.focusLabel(Vec2((ndc.x * 0.5f + 0.5f) * static_cast<f32>(size.width),
                              (0.5f - ndc.y * 0.5f) * static_cast<f32>(size.height)),
                         m_focus->name);
}
} // namespace g7

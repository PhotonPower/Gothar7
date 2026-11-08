// The editor's lights and mobs (M16 part D, docs/modules/editor.md): new light vobs from presets, the preview
// time of day, the light budget of E1 as a hint; mob values from lists (definitions, mobs.toml types), what a
// definition holds, its user slots as markers and the checks against NPCs, guilds and items.

#include <g7/core/StringUtil.hpp>
#include <g7/editor/Editor.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/Terrain.hpp>

#include <algorithm>
#include <format>

namespace g7::editor
{
Result<world::VobId> Editor::placeLight(std::string_view preset, std::optional<Vec3> at)
{
    const LightPreset* p = findLightPreset(preset);
    if (p == nullptr)
    {
        return Error{std::format("no light preset '{}'", preset)};
    }
    Vec3 position;
    if (at)
    {
        position = *at;
    }
    else
    {
        const render::Camera& camera = m_engine.camera();
        const Vec3 f = camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
        position =
            camera.transform.position + glm::normalize(Vec3(f.x, 0.0f, f.z) + Vec3(1e-4f, 0.0f, 0.0f)) * 4.0f;
        position.y =
            (m_engine.terrain() != nullptr ? m_engine.terrain()->heightAt(position.x, position.z) : 0.0f) +
            2.0f;
    }
    // Name: LIGHT_<PRESET>_NN, the first free number.
    std::string stem = "LIGHT_" + toUpper(p->name);
    std::string name;
    for (u32 n = 1;; ++n)
    {
        name = std::format("{}_{:02}", stem, n);
        bool taken = false;
        m_engine.scene().each<world::Vob>([&](entt::entity, const world::Vob& v)
                                          { taken = taken || v.nameText == name; });
        if (!taken)
        {
            break;
        }
    }
    world::VobId id;
    if (auto placed = edit("place " + name, {},
                           [&]() -> Result<void>
                           {
                               auto vob = editor::placeLight(m_engine.scene(), name, position, p->light);
                               if (!vob)
                               {
                                   return vob.error();
                               }
                               id = m_engine.scene().idOf(vob.value());
                               return {};
                           });
        !placed)
    {
        return placed.error();
    }
    select(id);
    m_status = std::format("placed {} (id {})", name, id.value);
    return id;
}

void Editor::setPreviewHour(f32 hour)
{
    const HourMinute t = splitHour(hour);
    world::GameTime& time = m_engine.gameTime();
    time.setTime(time.day(), t.hour, t.minute); // the sky, sun and window lights follow at the next frame
}

MobLists Editor::mobLists() const
{
    MobLists lists;
    lists.definitions = m_engine.mobDefinitions();
    lists.types = m_engine.mobTypeNames();
    lists.npcs = m_engine.scriptInstanceNames("Npc");
    lists.guilds = m_engine.guildNames();
    lists.items = m_engine.scriptInstanceNames("Item");
    return lists;
}

const std::vector<MobIssue>& Editor::checkMobs()
{
    m_mobLists = mobLists(); // scripts may have been reloaded
    m_mobIssues = editor::checkMobs(m_engine.scene(), m_mobLists);
    m_status = std::format("mobs: {} issue(s)", m_mobIssues.size());
    return m_mobIssues;
}

void Editor::drawMobSlots()
{
    const world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(m_selection);
    const world::MobRef* ref = scene.valid(e) ? scene.get<world::MobRef>(e) : nullptr;
    if (ref == nullptr)
    {
        return;
    }
    const auto mob = resolveMob(m_mobLists.definitions, m_mobLists.types, ref->definition);
    if (!mob)
    {
        return;
    }
    // In the mob's space as the engine places users: at the closed door (the slots belong to it).
    const Mat4 world = scene.get<world::WorldTransform>(e)->matrix;
    render::DebugDraw& draw = m_engine.debugDraw();
    const Vec4 colour(0.3f, 1.0f, 0.8f, 1.0f);
    for (const auto& [position, facing] : m_engine.mobTypeSlots(mob->type))
    {
        const Vec3 feet = Vec3(world * Vec4(position, 1.0f));
        const Vec3 look = glm::normalize(Vec3(world * Vec4(facing, 0.0f)) + Vec3(1e-6f));
        draw.circle(feet + Vec3(0.0f, 0.02f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.25f, {colour, 0.0f, false});
        draw.arrow(feet + Vec3(0.0f, 0.05f, 0.0f), feet + Vec3(0.0f, 0.05f, 0.0f) + look * 0.6f,
                   {colour, 0.0f, false});
        draw.line(feet, feet + Vec3(0.0f, 1.7f, 0.0f), {colour, 0.0f, false}); // a figure's height
    }
}
} // namespace g7::editor

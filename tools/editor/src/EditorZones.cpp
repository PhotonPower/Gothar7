// The editor's zone mode and trigger helpers (M16 part C, docs/modules/editor.md): indoor, music and ambient
// boxes (world.md "Zonen") selected, moved, turned about +Y and sized with the gizmo, their value from lists;
// which zone counts at the camera; the triggers' choice lists and checks. Every zone edit is undoable and
// goes to the engine at once (rooms, music and ambience follow).

#include <g7/core/StringUtil.hpp>
#include <g7/editor/Editor.hpp>
#include <g7/platform/Input.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/DebugUi.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/Terrain.hpp>

#include <algorithm>
#include <format>

namespace g7::editor
{
namespace
{
constexpr f32 kZoneDrawRadius = 250.0f; ///< m: boxes whose centre is this near (or around the camera)
constexpr f32 kZoneLabelRadius = 40.0f;
constexpr f32 kTriggerDrawRadius = 120.0f;

Vec4 zoneColour(std::string_view type)
{
    if (type == "indoor")
    {
        return Vec4(0.35f, 0.8f, 1.0f, 0.9f);
    }
    if (type == "music")
    {
        return Vec4(0.85f, 0.45f, 1.0f, 0.9f);
    }
    return Vec4(0.45f, 0.95f, 0.45f, 0.9f); // ambient
}

} // namespace

void Editor::setZoneMode(bool on)
{
    m_zoneMode = on;
    if (on)
    {
        m_waynetMode = false;
        m_terrainMode = false;
        m_waynetSelected.clear();
        select({});
    }
    else
    {
        m_zoneSelected.reset();
    }
}

void Editor::selectZone(std::optional<usize> index)
{
    m_zoneSelected = index && *index < m_zones.size() ? index : std::nullopt;
}

Result<void> Editor::editZones(std::string label,
                               const std::function<Result<void>(std::vector<world::Zone>&)>& change,
                               bool mergeable)
{
    std::vector<world::Zone> edited = m_zones;
    if (auto changed = change(edited); !changed)
    {
        m_status = changed.error().message;
        return changed;
    }
    if (auto handed = m_engine.setZones(edited); !handed)
    {
        m_status = handed.error().message;
        return handed;
    }
    Command command{std::move(label), {}, {}, mergeable};
    command.zonesBefore = std::exchange(m_zones, std::move(edited));
    command.zonesAfter = m_zones;
    m_history.record(std::move(command));
    return {};
}

Result<usize> Editor::addZone(std::string type, std::string value, const Vec3& centre)
{
    if (std::ranges::find(kZoneTypes, std::string_view(type)) == std::end(kZoneTypes))
    {
        return Error{std::format("zones of type '{}' are not edited here", type)};
    }
    if (value.empty())
    {
        return Error{std::format("a {} zone needs a value", type)};
    }
    const std::string label = std::format("add {} zone {}", type, value);
    if (auto added = editZones(label,
                               [&](std::vector<world::Zone>& zones) -> Result<void>
                               {
                                   zones.push_back(newZone(type, value, centre));
                                   return {};
                               });
        !added)
    {
        return added.error();
    }
    m_zoneSelected = m_zones.size() - 1;
    m_status = label;
    return m_zones.size() - 1;
}

Result<void> Editor::setZoneBox(const world::ZoneBox& box)
{
    if (!m_zoneSelected || !editableZone(m_zones[*m_zoneSelected]))
    {
        return Error{"no editable zone selected"};
    }
    const usize i = *m_zoneSelected;
    return editZones(
        std::format("zone {} ({}) box", m_zones[i].value, i),
        [i, box](std::vector<world::Zone>& zones) -> Result<void>
        {
            zones[i].box = world::ZoneBox{box.center, glm::max(box.halfExtents, Vec3(0.05f)), box.yawDegrees};
            return {};
        },
        true);
}

Result<void> Editor::setZoneValue(std::string value)
{
    if (!m_zoneSelected || !editableZone(m_zones[*m_zoneSelected]))
    {
        return Error{"no editable zone selected"};
    }
    const usize i = *m_zoneSelected;
    return editZones(
        std::format("zone {} value", i), // typing in the field: one step
        [i, value](std::vector<world::Zone>& zones) -> Result<void>
        {
            if (value.empty())
            {
                return Error{"a zone needs a value"};
            }
            zones[i].value = value;
            return {};
        },
        true);
}

Result<void> Editor::deleteZone()
{
    if (!m_zoneSelected)
    {
        return Error{"no zone selected"};
    }
    const usize i = *m_zoneSelected;
    const std::string label = std::format("delete zone {}", zoneLabel(m_zones[i]));
    if (auto removed = editZones(label,
                                 [i](std::vector<world::Zone>& zones) -> Result<void>
                                 {
                                     zones.erase(zones.begin() + static_cast<std::ptrdiff_t>(i));
                                     return {};
                                 });
        !removed)
    {
        return removed;
    }
    m_zoneSelected.reset();
    m_status = label;
    return {};
}

std::optional<usize> Editor::zoneAt(std::string_view type, const Vec3& point) const
{
    return smallestZoneIndex(m_zones, type, point);
}

std::vector<std::string> Editor::zoneValueChoices(std::string_view type) const
{
    return zoneValues(type, m_zones, m_engine.musicThemes(), m_engine.ambienceNames());
}

std::optional<std::vector<std::string>> Editor::startsOf(std::string_view worldPath)
{
    if (worldPath.empty())
    {
        return std::nullopt;
    }
    if (equalsIgnoreCase(worldPath, m_engine.worldPath()))
    {
        // The open world: as edited.
        std::vector<std::string> names;
        m_engine.scene().each<world::Vob, world::StartPoint>(
            [&](entt::entity, const world::Vob& vob, const world::StartPoint&)
            { names.push_back(vob.nameText); });
        return names;
    }
    if (const auto known = m_starts.find(worldPath); known != m_starts.end())
    {
        return known->second;
    }
    std::optional<std::vector<std::string>> names;
    if (auto file = world::loadWorldFile(m_engine.vfs(), worldPath); file)
    {
        names = startNames(file.value());
    }
    m_starts.emplace(std::string(worldPath), names);
    return names;
}

const std::vector<TriggerIssue>& Editor::checkTriggers()
{
    m_starts.clear();                               // other worlds may have changed on disk
    m_scriptFunctions = m_engine.scriptFunctions(); // scripts may have been reloaded
    m_triggerIssues =
        editor::checkTriggers(m_engine.scene(), m_engine.hasScripts() ? &m_scriptFunctions : nullptr,
                              [this](std::string_view w) { return startsOf(w); });
    m_status = std::format("triggers: {} issue(s)", m_triggerIssues.size());
    return m_triggerIssues;
}

void Editor::zoneMouse(Engine&, const Vec2& mouse, bool pressed, bool held)
{
    const GizmoView v = view();
    world::Zone* selected = m_zoneSelected ? &m_zones[*m_zoneSelected] : nullptr;
    const bool editable = selected != nullptr && editableZone(*selected);
    if (m_zoneDragging)
    {
        if (held && editable)
        {
            Transform t = m_zoneDragFrom;
            if (m_gizmo.mode == GizmoMode::Translate)
            {
                Vec3 delta = m_dragGizmo.dragTranslate(m_dragHandle, v, m_dragStart, mouse);
                if (snap)
                {
                    delta = Vec3(g7::editor::snap(delta.x, snapMove), g7::editor::snap(delta.y, snapMove),
                                 g7::editor::snap(delta.z, snapMove));
                }
                t.position += delta;
            }
            else if (m_gizmo.mode == GizmoMode::Rotate)
            {
                f32 angle = m_dragHandle == GizmoHandle::Y
                                ? m_dragGizmo.dragRotate(m_dragHandle, v, m_dragStart, mouse)
                                : 0.0f; // boxes turn about +Y only
                if (snap)
                {
                    angle = glm::radians(g7::editor::snap(glm::degrees(angle), snapAngle));
                }
                t.rotation =
                    glm::normalize(glm::angleAxis(angle, Vec3(0.0f, 1.0f, 0.0f)) * m_zoneDragFrom.rotation);
            }
            else
            {
                t.scale = m_zoneDragFrom.scale * m_dragGizmo.dragScale(m_dragHandle, v, m_dragStart, mouse);
            }
            std::vector<world::Zone> moved = m_zones;
            moved[*m_zoneSelected].box = zoneBoxFrom(t);
            if (m_engine.setZones(moved))
            {
                m_zones = std::move(moved); // one command at the end
            }
            return;
        }
        m_zoneDragging = false;
        m_dragHandle = GizmoHandle::None;
        Command command{"zone box", {}, {}, false};
        command.zonesBefore = std::exchange(m_zoneDragBefore, std::nullopt);
        command.zonesAfter = m_zones;
        if (command.zonesAfter != command.zonesBefore)
        {
            if (m_zoneSelected)
            {
                command.label = std::format("zone {} box", m_zones[*m_zoneSelected].value);
            }
            m_history.record(std::move(command));
        }
        return;
    }
    m_hover = GizmoHandle::None;
    if (editable)
    {
        const Transform t = zoneTransform(*selected->box);
        m_gizmo.origin = t.position;
        m_gizmo.axes = m_gizmo.mode == GizmoMode::Translate && !localAxes ? Mat3(1.0f) : Mat3(t.rotation);
        m_hover = m_gizmo.hit(v, mouse);
        if (m_gizmo.mode == GizmoMode::Rotate && m_hover != GizmoHandle::Y)
        {
            m_hover = GizmoHandle::None;
        }
    }
    if (!pressed)
    {
        return;
    }
    if (editable && m_hover != GizmoHandle::None)
    {
        m_zoneDragging = true;
        m_zoneDragBefore = m_zones;
        m_zoneDragFrom = zoneTransform(*selected->box);
        m_dragHandle = m_hover;
        m_dragStart = mouse;
        m_dragGizmo = m_gizmo;
        return;
    }
    selectZone(pickZone(m_zones, screenRay(v, mouse)));
}

void Editor::drawZones()
{
    render::DebugDraw& draw = m_engine.debugDraw();
    const Vec3 eye = m_engine.camera().transform.position;
    for (usize i = 0; i < m_zones.size(); ++i)
    {
        const world::Zone& z = m_zones[i];
        if (!z.box ||
            (glm::length(z.box->center - eye) > kZoneDrawRadius && !world::zoneContains(*z.box, eye)))
        {
            continue;
        }
        const bool sel = m_zoneSelected == i;
        const Vec4 colour = sel ? Vec4(1.0f, 0.95f, 0.2f, 1.0f) : zoneColour(z.type);
        draw.box(zoneMatrix(*z.box), z.box->halfExtents, {colour});
        if (sel || glm::length(z.box->center - eye) < kZoneLabelRadius)
        {
            draw.text(z.box->center, std::format("{} {}", z.type, z.value), {colour});
        }
    }
    if (m_zoneSelected && editableZone(m_zones[*m_zoneSelected]))
    {
        drawGizmo();
    }
}

void Editor::drawTriggers()
{
    // Every trigger's volume (the selection's bounds come from drawSelection): boxes turned and scaled with
    // the vob, spheres scaled by its largest axis - as the trigger system measures.
    render::DebugDraw& draw = m_engine.debugDraw();
    const Vec3 eye = m_engine.camera().transform.position;
    const Vec4 colour(1.0f, 0.55f, 0.15f, 0.9f);
    m_engine.scene().each<world::Vob, world::TriggerVolume, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::TriggerVolume& t,
            const world::WorldTransform& w)
        {
            const Vec3 at = Vec3(w.matrix[3]);
            if (glm::length(at - eye) > kTriggerDrawRadius)
            {
                return;
            }
            if (t.shape == world::TriggerVolume::Shape::Box)
            {
                draw.box(w.matrix, t.halfExtents, {colour});
            }
            else
            {
                const f32 scale = std::max({glm::length(Vec3(w.matrix[0])), glm::length(Vec3(w.matrix[1])),
                                            glm::length(Vec3(w.matrix[2]))});
                draw.sphere(at, t.radius * scale, {colour});
            }
            if (glm::length(at - eye) < kZoneLabelRadius)
            {
                draw.text(at + Vec3(0.0f, 0.5f, 0.0f), vob.nameText, {colour});
            }
        });
}

void Editor::fillZonePanel()
{
    ui::ZonePanel& p = m_zonePanel;
    p.shown = true;
    p.active = m_zoneMode;
    p.rows.clear();
    for (const world::Zone& z : m_zones)
    {
        p.rows.push_back(zoneLabel(z));
    }
    p.types.assign(std::begin(kZoneTypes), std::end(kZoneTypes));
    p.newType = std::clamp<i32>(p.newType, 0, static_cast<i32>(p.types.size()) - 1);
    p.newValues = zoneValueChoices(p.types[static_cast<usize>(p.newType)]);
    const i32 selected = m_zoneSelected ? static_cast<i32>(*m_zoneSelected) : -1;
    const world::Zone* sel = m_zoneSelected ? &m_zones[*m_zoneSelected] : nullptr;
    if (selected != p.selected || (sel != nullptr && !p.valueChanged))
    {
        p.value = sel != nullptr ? sel->value : std::string();
    }
    p.selected = selected;
    p.selectedEditable = sel != nullptr && editableZone(*sel);
    p.selectedType = sel != nullptr ? sel->type : std::string();
    p.values = sel != nullptr ? zoneValueChoices(sel->type) : std::vector<std::string>{};
    if (sel != nullptr && sel->box)
    {
        p.center = sel->box->center;
        p.halfExtents = sel->box->halfExtents;
        p.yawDegrees = sel->box->yawDegrees;
    }
    const Vec3 eye = m_engine.camera().transform.position;
    p.here.clear();
    for (const std::string_view type : kZoneTypes)
    {
        const auto at = zoneAt(type, eye);
        p.here += std::format("{}{} {}", p.here.empty() ? "" : ", ", type, at ? m_zones[*at].value : "-");
    }
    p.triggerIssues.clear();
    for (const TriggerIssue& issue : m_triggerIssues)
    {
        p.triggerIssues.push_back(std::format("{} ({}): {}", issue.name, issue.id.value, issue.text));
    }
}

void Editor::applyZonePanel()
{
    ui::ZonePanel& p = m_zonePanel;
    if (p.active != m_zoneMode)
    {
        setZoneMode(p.active);
    }
    if (p.clickedRow >= 0)
    {
        if (!m_zoneMode)
        {
            setZoneMode(true);
        }
        selectZone(static_cast<usize>(p.clickedRow));
    }
    if (p.add && !p.newValue.empty())
    {
        const render::Camera& camera = m_engine.camera();
        const Vec3 f = camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
        Vec3 at =
            camera.transform.position + glm::normalize(Vec3(f.x, 0.0f, f.z) + Vec3(1e-4f, 0.0f, 0.0f)) * 6.0f;
        if (m_engine.terrain() != nullptr)
        {
            at.y = m_engine.terrain()->heightAt(at.x, at.z);
        }
        const std::string type(kZoneTypes[static_cast<usize>(p.newType)]);
        at.y += type == "indoor" ? 1.5f : 10.0f; // the box stands on the ground
        if (!m_zoneMode)
        {
            setZoneMode(true);
        }
        (void)addZone(type, p.newValue, at);
    }
    if (p.boxChanged)
    {
        (void)setZoneBox(world::ZoneBox{p.center, p.halfExtents, p.yawDegrees});
    }
    if (p.valueChanged && !p.value.empty())
    {
        (void)setZoneValue(p.value);
    }
    if (p.remove)
    {
        (void)deleteZone();
    }
    if (p.checkTriggers)
    {
        (void)checkTriggers();
    }
    if (p.clickedIssue >= 0 && static_cast<usize>(p.clickedIssue) < m_triggerIssues.size())
    {
        if (m_zoneMode)
        {
            setZoneMode(false);
        }
        select(m_triggerIssues[static_cast<usize>(p.clickedIssue)].id);
    }
}
} // namespace g7::editor

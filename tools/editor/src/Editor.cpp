#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/editor/Editor.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/platform/Input.hpp>
#include <g7/platform/Window.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/DebugUi.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/Terrain.hpp>
#include <g7/world/WorldFile.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <numbers>

namespace g7::editor
{
namespace
{
constexpr Vec4 kAxisColors[3] = {Vec4(0.95f, 0.25f, 0.25f, 1.0f), Vec4(0.3f, 0.9f, 0.3f, 1.0f),
                                 Vec4(0.3f, 0.5f, 1.0f, 1.0f)};
constexpr Vec4 kActive(1.0f, 0.9f, 0.2f, 1.0f);

std::string_view typeOf(const world::Scene& scene, entt::entity e)
{
    if (scene.get<world::MobRef>(e))
    {
        return "mob";
    }
    if (scene.get<world::MeshRef>(e))
    {
        return "mesh";
    }
    if (scene.get<world::LightSource>(e))
    {
        return "light";
    }
    if (scene.has<world::StartPoint>(e))
    {
        return "start";
    }
    if (scene.get<world::SoundEmitter>(e))
    {
        return "sound";
    }
    if (scene.get<world::TriggerVolume>(e))
    {
        return "trigger";
    }
    return "empty";
}

Vec3 eulerDegrees(const Quat& q)
{
    return glm::degrees(glm::eulerAngles(q));
}
} // namespace

Editor::Editor(Engine& engine) : m_engine(engine)
{
    engine.setPaused(true);
    engine.setDebugUiVisible(true);
    engine.setDebugOverlay(true);
    // Placeable models: glTF sources and cooked meshes (a cooked one only if its source is not there).
    for (const auto& file : engine.vfs().list({}, ".glb"))
    {
        m_assets.push_back(file.path);
    }
    for (const auto& file : engine.vfs().list({}, ".g7mesh"))
    {
        const std::string source = file.path.substr(0, file.path.size() - 7) + ".glb";
        if (!std::binary_search(m_assets.begin(), m_assets.end(), source))
        {
            m_assets.push_back(file.path);
        }
    }
    std::sort(m_assets.begin(), m_assets.end());
    G7_LOG_INFO("editor", "editor mode: {} models to place; left click selects, right mouse looks",
                m_assets.size());
}

GizmoView Editor::view() const
{
    // Mouse positions are in window coordinates (not pixels): the view uses the window size.
    Vec2 size(1280.0f, 720.0f);
    if (const platform::Window* window = const_cast<Engine&>(m_engine).window())
    {
        size = Vec2(static_cast<f32>(window->size().width), static_cast<f32>(window->size().height));
    }
    const render::Camera& camera = const_cast<Engine&>(m_engine).camera();
    return {camera.viewProjection(), camera.transform.position, size};
}

void Editor::select(world::VobId id)
{
    m_selection = id;
    m_dragHandle = GizmoHandle::None;
}

void Editor::placeGizmo()
{
    const world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(m_selection);
    if (!scene.valid(e))
    {
        m_selection = {};
        return;
    }
    const Transform t = worldTransformOf(scene, e);
    m_gizmo.origin = t.position;
    m_gizmo.axes = localAxes || m_gizmo.mode == GizmoMode::Scale ? Mat3(t.rotation) : Mat3(1.0f);
}

void Editor::sceneChanged()
{
    m_dirty = true;
    m_engine.scene().updateTransforms();
    if (auto refreshed = m_engine.refreshScene(); !refreshed)
    {
        m_status = refreshed.error().message;
    }
}

void Editor::setSelectionTransform(const Transform& world)
{
    world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(m_selection);
    if (!scene.valid(e))
    {
        return;
    }
    setWorldTransform(scene, e, world);
    sceneChanged();
}

Result<world::VobId> Editor::place(std::string_view meshPath)
{
    // On the ground about 8 m ahead (terrain height if there is terrain, else the camera's ground y 0).
    const render::Camera& camera = m_engine.camera();
    const Vec3 forward = camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    Vec3 position = camera.transform.position + glm::normalize(Vec3(forward.x, 0.0f, forward.z)) * 8.0f;
    position.y = m_engine.terrain() != nullptr ? m_engine.terrain()->heightAt(position.x, position.z) : 0.0f;
    if (snap)
    {
        position.x = g7::editor::snap(position.x, snapMove);
        position.z = g7::editor::snap(position.z, snapMove);
    }
    world::Scene& scene = m_engine.scene();
    auto vob = placeMesh(scene, meshPath, position);
    if (!vob)
    {
        return vob.error();
    }
    const world::VobId id = scene.idOf(vob.value());
    sceneChanged();
    select(id);
    m_status = std::format("placed {} (id {})", meshPath, id.value);
    return id;
}

Result<world::VobId> Editor::duplicateSelection()
{
    world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(m_selection);
    if (!scene.valid(e))
    {
        return Error{"nothing selected"};
    }
    auto copy = duplicateVob(scene, e, Vec3(snap ? std::max(snapMove, 1.0f) : 1.0f, 0.0f, 0.0f));
    if (!copy)
    {
        return copy.error();
    }
    const world::VobId id = scene.idOf(copy.value());
    sceneChanged();
    select(id);
    m_status = std::format("duplicated as id {}", id.value);
    return id;
}

void Editor::deleteSelection()
{
    world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(m_selection);
    if (!scene.valid(e))
    {
        return;
    }
    m_status = std::format("deleted id {} (the id stays used)", m_selection.value);
    removeVob(scene, e);
    m_selection = {};
    sceneChanged();
}

Result<void> Editor::save()
{
    const auto path = m_engine.worldSourceFile();
    if (!path)
    {
        m_status = "cannot save: the world is not a loose file (archive or no world)";
        return Error{m_status};
    }
    std::error_code ec;
    if (!m_backedUp && std::filesystem::exists(*path, ec))
    {
        // No undo yet: the version before this session's first save stays next to it.
        fs::Path backup = *path;
        backup += ".bak";
        std::filesystem::copy_file(*path, backup, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            m_status = "cannot write the backup " + fs::toUtf8(backup) + ": " + ec.message();
            return Error{m_status};
        }
        m_backedUp = true;
    }
    if (auto saved = m_engine.saveWorld(*path); !saved)
    {
        m_status = saved.error().message;
        return saved;
    }
    m_dirty = false;
    m_status = "saved " + fs::toUtf8(*path);
    return {};
}

std::string Editor::warningFor(world::VobId id) const
{
    if (id.valid() && world::isGenerated(m_engine.worldFile(), id))
    {
        return "Generated vob: the next 'gothar-worldgen assemble' rewrites it - set \"locked\" there to "
               "keep "
               "editor changes.";
    }
    return {};
}

void Editor::update(Engine& engine, f64, bool uiMouse, bool uiKeyboard)
{
    const platform::Input& input = engine.input();
    if (!uiKeyboard)
    {
        const bool ctrl = input.isDown(platform::Key::LeftCtrl) || input.isDown(platform::Key::RightCtrl);
        if (input.pressed(platform::Key::Num1))
        {
            m_gizmo.mode = GizmoMode::Translate;
        }
        if (input.pressed(platform::Key::Num2))
        {
            m_gizmo.mode = GizmoMode::Rotate;
        }
        if (input.pressed(platform::Key::Num3))
        {
            m_gizmo.mode = GizmoMode::Scale;
        }
        if (input.pressed(platform::Key::L))
        {
            localAxes = !localAxes;
        }
        if (input.pressed(platform::Key::X))
        {
            snap = !snap;
        }
        if (input.pressed(platform::Key::Escape))
        {
            select({});
        }
        if (input.pressed(platform::Key::Delete))
        {
            deleteSelection();
        }
        if (ctrl && input.pressed(platform::Key::D))
        {
            (void)duplicateSelection();
        }
        if (ctrl && input.pressed(platform::Key::S))
        {
            (void)save();
        }
    }

    placeGizmo();
    const Vec2 mouse = input.mousePosition();
    const GizmoView v = view();
    if (m_dragHandle != GizmoHandle::None)
    {
        if (input.isDown(platform::MouseButton::Left))
        {
            drag(mouse);
        }
        else
        {
            m_dragHandle = GizmoHandle::None;
        }
    }
    else if (!uiMouse)
    {
        m_hover = m_selection.valid() ? m_gizmo.hit(v, mouse) : GizmoHandle::None;
        if (input.pressed(platform::MouseButton::Left))
        {
            if (m_hover != GizmoHandle::None)
            {
                m_dragHandle = m_hover;
                m_dragStart = mouse;
                m_dragGizmo = m_gizmo;
                m_dragFrom = worldTransformOf(engine.scene(), engine.scene().findById(m_selection));
            }
            else
            {
                select(pick(engine.scene(), engine.instances(), screenRay(v, mouse)));
            }
        }
    }
    drawSelection();
}

void Editor::drag(const Vec2& mouse)
{
    const GizmoView v = view();
    Transform t = m_dragFrom;
    if (m_gizmo.mode == GizmoMode::Translate)
    {
        Vec3 delta = m_dragGizmo.dragTranslate(m_dragHandle, v, m_dragStart, mouse);
        if (snap)
        {
            // Snap the movement along each gizmo axis (so local axes snap in their own grid).
            Vec3 snapped(0.0f);
            for (int a = 0; a < 3; ++a)
            {
                const Vec3 axis = m_dragGizmo.axes[a];
                snapped += axis * g7::editor::snap(glm::dot(delta, axis), snapMove);
            }
            delta = snapped;
        }
        t.position += delta;
    }
    else if (m_gizmo.mode == GizmoMode::Rotate)
    {
        f32 angle = m_dragGizmo.dragRotate(m_dragHandle, v, m_dragStart, mouse);
        if (snap)
        {
            angle = glm::radians(g7::editor::snap(glm::degrees(angle), snapAngle));
        }
        t.rotation =
            glm::normalize(glm::angleAxis(angle, m_dragGizmo.axisOf(m_dragHandle)) * m_dragFrom.rotation);
    }
    else
    {
        Vec3 factor = m_dragGizmo.dragScale(m_dragHandle, v, m_dragStart, mouse);
        if (snap)
        {
            factor = Vec3(g7::editor::snap(factor.x, 0.1f), g7::editor::snap(factor.y, 0.1f),
                          g7::editor::snap(factor.z, 0.1f));
            factor = glm::max(factor, Vec3(0.1f));
        }
        t.scale = m_dragFrom.scale * factor;
    }
    setSelectionTransform(t);
    placeGizmo();
}

void Editor::drawSelection()
{
    world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(m_selection);
    if (!scene.valid(e))
    {
        return;
    }
    render::DebugDraw& draw = m_engine.debugDraw();
    // The selection: its drawn bounds if it has a mesh, else a small box at its place.
    bool boxed = false;
    for (const SceneInstance& instance : m_engine.instances())
    {
        if (instance.vob == m_selection)
        {
            draw.box(instance.bounds, {kActive});
            boxed = true;
        }
    }
    if (!boxed)
    {
        draw.box(AABB{m_gizmo.origin - Vec3(0.3f), m_gizmo.origin + Vec3(0.3f)}, {kActive});
    }

    // The gizmo, on top of everything.
    const GizmoView v = view();
    const f32 arm = m_gizmo.armLength(v);
    const GizmoHandle active = m_dragHandle != GizmoHandle::None ? m_dragHandle : m_hover;
    constexpr GizmoHandle kAxes[3] = {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};
    for (int a = 0; a < 3; ++a)
    {
        const render::DebugStyle style{active == kAxes[a] ? kActive : kAxisColors[a], 0.0f, false};
        const Vec3 axis = m_gizmo.axes[a];
        switch (m_gizmo.mode)
        {
        case GizmoMode::Translate:
            draw.arrow(m_gizmo.origin, m_gizmo.origin + axis * arm, style);
            break;
        case GizmoMode::Rotate:
            draw.circle(m_gizmo.origin, axis, arm, style);
            break;
        case GizmoMode::Scale:
            draw.line(m_gizmo.origin, m_gizmo.origin + axis * arm, style);
            draw.box(AABB{m_gizmo.origin + axis * arm - Vec3(arm * 0.05f),
                          m_gizmo.origin + axis * arm + Vec3(arm * 0.05f)},
                     style);
            break;
        }
    }
    if (m_gizmo.mode == GizmoMode::Translate)
    {
        constexpr GizmoHandle kPlanes[3] = {GizmoHandle::PlaneXY, GizmoHandle::PlaneYZ, GizmoHandle::PlaneXZ};
        constexpr int kPairs[3][2] = {{0, 1}, {1, 2}, {0, 2}};
        for (int p = 0; p < 3; ++p)
        {
            const Vec3 u = m_gizmo.axes[kPairs[p][0]] * arm;
            const Vec3 w = m_gizmo.axes[kPairs[p][1]] * arm;
            const Vec3 o = m_gizmo.origin;
            const Vec4 colour = active == kPlanes[p] ? kActive : Vec4(0.8f, 0.8f, 0.8f, 0.8f);
            const render::DebugStyle style{colour, 0.0f, false};
            const Vec3 c[4] = {o + u * Gizmo::kPlaneFrom + w * Gizmo::kPlaneFrom,
                               o + u * Gizmo::kPlaneTo + w * Gizmo::kPlaneFrom,
                               o + u * Gizmo::kPlaneTo + w * Gizmo::kPlaneTo,
                               o + u * Gizmo::kPlaneFrom + w * Gizmo::kPlaneTo};
            for (int i = 0; i < 4; ++i)
            {
                draw.line(c[i], c[(i + 1) % 4], style);
            }
        }
    }
    if (m_gizmo.mode == GizmoMode::Scale)
    {
        const f32 half = arm * Gizmo::kUniformPixels / Gizmo::kArmPixels;
        draw.box(AABB{m_gizmo.origin - Vec3(half), m_gizmo.origin + Vec3(half)},
                 {active == GizmoHandle::Uniform ? kActive : Vec4(0.8f, 0.8f, 0.8f, 1.0f), 0.0f, false});
    }
}

void Editor::fillPanel()
{
    ui::EditorPanel& p = m_panel;
    const world::Scene& scene = m_engine.scene();
    p.world = m_engine.worldPath();
    p.dirty = m_dirty;
    p.canSave = m_engine.worldSourceFile().has_value();
    p.status = m_status;
    p.warning = warningFor(m_selection);
    p.gizmoMode = static_cast<i32>(m_gizmo.mode);
    p.localAxes = localAxes;
    p.snap = snap;
    p.snapMove = snapMove;
    p.snapAngle = snapAngle;
    p.assets = m_assets;

    // The vob tree: roots by id, children below their parent.
    p.vobs.clear();
    std::multimap<u64, entt::entity> roots;
    scene.each<world::Vob>(
        [&](entt::entity e, const world::Vob& vob)
        {
            if (!scene.valid(scene.parent(e)))
                roots.emplace(vob.id.value, e);
        });
    std::function<void(entt::entity, u32)> add = [&](entt::entity e, u32 depth)
    {
        const world::Vob* vob = scene.get<world::Vob>(e);
        p.vobs.push_back({vob->id.value,
                          std::format("{} ({}, {})", vob->nameText, typeOf(scene, e), vob->id.value), depth,
                          vob->id == m_selection});
        std::vector<entt::entity> children = scene.children(e);
        std::sort(children.begin(), children.end(),
                  [&](entt::entity a, entt::entity b) { return scene.idOf(a) < scene.idOf(b); });
        for (const entt::entity child : children)
        {
            add(child, depth + 1);
        }
    };
    for (const auto& [id, e] : roots)
    {
        add(e, 0);
    }

    // Properties of the selection.
    p.fields.clear();
    const entt::entity e = scene.findById(m_selection);
    if (!scene.valid(e))
    {
        return;
    }
    using Kind = ui::EditorField::Kind;
    const auto addField = [&](std::string label, Kind kind) -> ui::EditorField&
    {
        p.fields.push_back({});
        p.fields.back().label = std::move(label);
        p.fields.back().kind = kind;
        return p.fields.back();
    };
    const world::Vob* vob = scene.get<world::Vob>(e);
    addField("id", Kind::ReadOnly).text = std::format("{} ({})", vob->id.value, typeOf(scene, e));
    addField("name", Kind::Text).text = vob->nameText;
    const Transform t = worldTransformOf(scene, e);
    addField("position", Kind::Vector).vector = t.position;
    addField("rotation (deg)", Kind::Vector).vector = eulerDegrees(t.rotation);
    addField("scale", Kind::Vector).vector = t.scale;
    if (const auto* mesh = scene.get<world::MeshRef>(e))
    {
        addField("mesh", Kind::ReadOnly).text = mesh->path;
        if (!scene.get<world::MobRef>(e))
        {
            auto& category = addField("category", Kind::Choice);
            category.choices = {"deco", "gameplay"};
            category.choice = mesh->category == world::VobCategory::Gameplay ? 1 : 0;
        }
    }
    if (const auto* mob = scene.get<world::MobRef>(e))
    {
        addField("mob definition", Kind::Text).text = mob->definition;
    }
    if (const auto* light = scene.get<world::LightSource>(e))
    {
        addField("light colour", Kind::Color).vector = light->color;
        addField("light range", Kind::Number).number = light->range;
        addField("light intensity", Kind::Number).number = light->intensity;
        addField("light flicker", Kind::Number).number = light->flicker;
    }
    if (const auto* sound = scene.get<world::SoundEmitter>(e))
    {
        addField("sound", Kind::Text).text = sound->sound;
        addField("sound range", Kind::Number).number = sound->range;
        addField("sound volume", Kind::Number).number = sound->volume;
    }
    if (const auto* trigger = scene.get<world::TriggerVolume>(e))
    {
        if (trigger->shape == world::TriggerVolume::Shape::Box)
        {
            addField("half extents", Kind::Vector).vector = trigger->halfExtents;
        }
        else
        {
            addField("radius", Kind::Number).number = trigger->radius;
        }
        addField("on enter", Kind::Text).text = trigger->onEnter;
        addField("on leave", Kind::Text).text = trigger->onLeave;
        addField("once", Kind::Flag).flag = trigger->once;
        addField("change world", Kind::Text).text = trigger->changeWorld;
        addField("change start", Kind::Text).text = trigger->changeStart;
    }
}

void Editor::applyPanel()
{
    ui::EditorPanel& p = m_panel;
    m_gizmo.mode = static_cast<GizmoMode>(std::clamp(p.gizmoMode, 0, 2));
    localAxes = p.localAxes;
    snap = p.snap;
    snapMove = std::max(p.snapMove, 0.01f);
    snapAngle = std::max(p.snapAngle, 1.0f);
    if (p.clickedVob != 0)
    {
        select(world::VobId{p.clickedVob});
    }
    if (p.placeAsset >= 0 && static_cast<usize>(p.placeAsset) < m_assets.size())
    {
        (void)place(m_assets[static_cast<usize>(p.placeAsset)]);
    }
    if (p.save)
    {
        (void)save();
    }
    if (p.duplicate)
    {
        (void)duplicateSelection();
    }
    if (p.remove)
    {
        deleteSelection();
        return;
    }

    world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(m_selection);
    if (!scene.valid(e) || p.clickedVob != 0)
    {
        return;
    }
    bool changed = false;
    Transform t = worldTransformOf(scene, e);
    bool moved = false;
    for (const ui::EditorField& f : p.fields)
    {
        if (!f.changed)
        {
            continue;
        }
        changed = true;
        if (f.label == "name")
        {
            world::Vob vob = *scene.get<world::Vob>(e);
            vob.nameText = f.text;
            vob.name = StringId(f.text);
            scene.set<world::Vob>(e, vob);
        }
        else if (f.label == "position")
        {
            t.position = f.vector;
            moved = true;
        }
        else if (f.label == "rotation (deg)")
        {
            t.rotation = Quat(glm::radians(f.vector));
            moved = true;
        }
        else if (f.label == "scale")
        {
            t.scale = glm::max(f.vector, Vec3(0.01f));
            moved = true;
        }
        else if (f.label == "category")
        {
            scene.get<world::MeshRef>(e)->category =
                f.choice == 1 ? world::VobCategory::Gameplay : world::VobCategory::Deco;
        }
        else if (f.label == "mob definition")
        {
            scene.get<world::MobRef>(e)->definition = f.text;
        }
        else if (auto* light = scene.get<world::LightSource>(e); light && f.label.starts_with("light "))
        {
            light->color = f.label == "light colour" ? f.vector : light->color;
            light->range = f.label == "light range" ? std::max(f.number, 0.1f) : light->range;
            light->intensity = f.label == "light intensity" ? std::max(f.number, 0.0f) : light->intensity;
            light->flicker = f.label == "light flicker" ? std::clamp(f.number, 0.0f, 1.0f) : light->flicker;
        }
        else if (auto* sound = scene.get<world::SoundEmitter>(e); sound && f.label.starts_with("sound"))
        {
            sound->sound = f.label == "sound" ? f.text : sound->sound;
            sound->range = f.label == "sound range" ? std::max(f.number, 0.1f) : sound->range;
            sound->volume = f.label == "sound volume" ? std::clamp(f.number, 0.0f, 1.0f) : sound->volume;
        }
        else if (auto* trigger = scene.get<world::TriggerVolume>(e))
        {
            if (f.label == "half extents")
            {
                trigger->halfExtents = glm::max(f.vector, Vec3(0.05f));
            }
            else if (f.label == "radius")
            {
                trigger->radius = std::max(f.number, 0.05f);
            }
            else if (f.label == "on enter")
            {
                trigger->onEnter = f.text;
            }
            else if (f.label == "on leave")
            {
                trigger->onLeave = f.text;
            }
            else if (f.label == "once")
            {
                trigger->once = f.flag;
            }
            else if (f.label == "change world")
            {
                trigger->changeWorld = f.text;
            }
            else if (f.label == "change start")
            {
                trigger->changeStart = f.text;
            }
        }
    }
    if (moved)
    {
        setWorldTransform(scene, e, t);
    }
    if (changed)
    {
        sceneChanged();
    }
}

void Editor::ui(Engine&, ui::DebugUi& debugUi)
{
    fillPanel();
    debugUi.editorPanel(m_panel);
    applyPanel();
}
} // namespace g7::editor

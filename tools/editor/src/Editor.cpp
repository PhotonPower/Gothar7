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
#include <g7/world/WorldScene.hpp>

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

Editor::Editor(Engine& engine)
    : m_engine(engine), m_history(static_cast<usize>(
                            std::clamp<i64>(engine.settings().get<i64>("editor.undo_depth", 200), 1, 10000)))
{
    engine.setPaused(true);
    m_waynetRadius = std::clamp(static_cast<f32>(engine.settings().get<f64>("editor.waynet_radius", 80.0)),
                                10.0f, 2000.0f);
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
    m_waynetData = engine.worldFile().waynet.value_or(world::WaynetData{}); // edited as a copy (part B)
    m_zones = engine.worldFile().zones;                                     // part C
    for (const auto& file : engine.vfs().list({}, ".g7world"))
    {
        m_worlds.push_back(file.path);
    }
    std::sort(m_worlds.begin(), m_worlds.end());
    m_scriptFunctions = engine.scriptFunctions();
    m_mobLists = mobLists();                 // part D
    for (RoutineInfo& r : engine.routines()) // part E: the routines of the editor's file
    {
        if (r.file == editorRoutineFile())
        {
            m_routines.push_back(std::move(r));
        }
    }
    m_routinesWritten = m_routines;
    // Part F: the templates next to the assets on disk (assets/source/prefabs).
    if (const auto data = engine.vfs().diskPath("data/mobs.toml"))
    {
        m_prefabDirectory = data->parent_path().parent_path() / "prefabs";
    }
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
    m_engine.scene().updateTransforms();
    if (auto refreshed = m_engine.refreshScene(); !refreshed)
    {
        m_status = refreshed.error().message;
    }
}

std::string Editor::vobLabel(world::VobId id) const
{
    const world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(id);
    const world::Vob* vob = scene.valid(e) ? scene.get<world::Vob>(e) : nullptr;
    return vob != nullptr ? std::format("{} ({})", vob->nameText, id.value) : std::format("id {}", id.value);
}

void Editor::ask(std::string question, std::function<void()> onYes)
{
    m_confirm = std::move(question);
    m_onYes = std::move(onYes);
}

void Editor::confirm(bool yes)
{
    if (m_confirm.empty())
    {
        return;
    }
    m_confirm.clear();
    auto onYes = std::exchange(m_onYes, {});
    if (yes && onYes)
    {
        onYes();
    }
}

bool Editor::mayEdit(std::span<const world::VobId> ids, std::function<void()> retry)
{
    if (m_generatedConfirmed)
    {
        return true;
    }
    for (const world::VobId id : ids)
    {
        if (id.valid() && world::isGenerated(m_engine.worldFile(), id))
        {
            ask(std::format(
                    "{} is generated: the next 'gothar-worldgen assemble' writes it anew and the change "
                    "is lost (welt's annotations keep changes). Edit generated vobs anyway?",
                    vobLabel(id)),
                [this, retry = std::move(retry)]
                {
                    m_generatedConfirmed = true;
                    retry();
                });
            return false;
        }
    }
    return true;
}

Result<void> Editor::edit(std::string label, std::vector<world::VobId> ids,
                          const std::function<Result<void>()>& change, bool mergeable)
{
    if (!mayEdit(ids, [this, label, ids, change, mergeable] { (void)edit(label, ids, change, mergeable); }))
    {
        return Error{"waiting for confirmation"};
    }
    world::Scene& scene = m_engine.scene();
    const u64 firstNew = scene.nextVobId();
    Command command{std::move(label), captureVobs(scene, ids), {}, mergeable};
    if (auto changed = change(); !changed)
    {
        m_status = changed.error().message;
        return changed;
    }
    // Vobs the change created: their ids come from the counter; before it they were not there.
    for (u64 id = firstNew; id < scene.nextVobId(); ++id)
    {
        ids.push_back(world::VobId{id});
        command.before.push_back({world::VobId{id}, std::nullopt});
    }
    command.after = captureVobs(scene, ids);
    for (const VobState& s : command.before)
    {
        if (world::isGenerated(m_engine.worldFile(), s.id))
        {
            m_generatedEdited.insert(s.id.value);
        }
    }
    m_history.record(std::move(command));
    sceneChanged();
    return {};
}

Result<bool> Editor::undo()
{
    const std::string label = m_history.undoLabel();
    auto undone = m_history.undo(m_engine.scene(), &m_waynetData, &m_zones, &m_routines);
    if (undone && undone.value())
    {
        historyApplied();
        m_status = "undone: " + label;
    }
    return undone;
}

void Editor::historyApplied()
{
    sceneChanged();
    if (auto handed = m_engine.setWaynet(m_waynetData); !handed)
    {
        m_status = handed.error().message;
    }
    if (m_waynetData.findPoint(m_waynetSelected) == nullptr &&
        m_waynetData.findFreepoint(m_waynetSelected) == nullptr)
    {
        m_waynetSelected.clear();
    }
    if (auto handed = m_engine.setZones(m_zones); !handed)
    {
        m_status = handed.error().message;
    }
    selectZone(m_zoneSelected); // gone after undoing its "add"
}

Result<bool> Editor::redo()
{
    const std::string label = m_history.redoLabel();
    auto redone = m_history.redo(m_engine.scene(), &m_waynetData, &m_zones, &m_routines);
    if (redone && redone.value())
    {
        historyApplied();
        m_status = "redone: " + label;
    }
    return redone;
}

void Editor::setSelectionTransform(const Transform& world)
{
    if (!m_engine.scene().valid(m_engine.scene().findById(m_selection)))
    {
        return;
    }
    (void)edit("move " + vobLabel(m_selection), {m_selection},
               [this, world]
               {
                   setWorldTransform(m_engine.scene(), m_engine.scene().findById(m_selection), world);
                   return Result<void>{};
               });
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
    world::VobId id;
    const std::string path(meshPath);
    if (auto placed = edit("place " + path, {},
                           [&]() -> Result<void>
                           {
                               auto vob = placeMesh(m_engine.scene(), path, position);
                               if (!vob)
                               {
                                   return vob.error();
                               }
                               id = m_engine.scene().idOf(vob.value());
                               // ADR 0023: empty VOB_<type>_<name> nodes of a .glb become child vobs.
                               m_importWarnings.clear();
                               auto markers = m_engine.modelMarkers(path, "VOB_");
                               if (!markers)
                               {
                                   m_importWarnings.push_back(markers.error().message);
                                   return {};
                               }
                               std::vector<VobMarker> found;
                               for (const Engine::ModelMarker& m : markers.value())
                               {
                                   found.push_back({m.name, m.local});
                               }
                               VobImport imported = vobsFromMarkers(found);
                               m_importWarnings = std::move(imported.warnings);
                               for (world::WorldFileVob& v : imported.vobs)
                               {
                                   v.id = world::VobId{m_engine.scene().nextVobId()};
                                   v.parent = id;
                                   if (auto restored = world::restoreVob(m_engine.scene(), v); !restored)
                                   {
                                       return restored.error();
                                   }
                                   if (m_engine.scene().nextVobId() <= v.id.value)
                                   {
                                       (void)m_engine.scene().setNextVobId(v.id.value + 1);
                                   }
                               }
                               return {};
                           });
        !placed)
    {
        return placed.error();
    }
    select(id);
    m_status = std::format("placed {} (id {})", meshPath, id.value);
    return id;
}

Result<world::VobId> Editor::duplicateSelection()
{
    if (!m_engine.scene().valid(m_engine.scene().findById(m_selection)))
    {
        return Error{"nothing selected"};
    }
    world::VobId id;
    const world::VobId source = m_selection;
    const Vec3 offset(snap ? std::max(snapMove, 1.0f) : 1.0f, 0.0f, 0.0f);
    if (auto copied = edit("duplicate " + vobLabel(source), {},
                           [&]() -> Result<void>
                           {
                               auto copy =
                                   duplicateVob(m_engine.scene(), m_engine.scene().findById(source), offset);
                               if (!copy)
                               {
                                   return copy.error();
                               }
                               id = m_engine.scene().idOf(copy.value());
                               return {};
                           });
        !copied)
    {
        return copied.error();
    }
    select(id);
    m_status = std::format("duplicated as id {}", id.value);
    return id;
}

void Editor::deleteSelection()
{
    if (!m_engine.scene().valid(m_engine.scene().findById(m_selection)))
    {
        return;
    }
    const world::VobId id = m_selection;
    if (edit("delete " + vobLabel(id), {id},
             [this, id]
             {
                 removeVob(m_engine.scene(), m_engine.scene().findById(id));
                 return Result<void>{};
             }))
    {
        m_status = std::format("deleted id {} (the id stays used; Ctrl+Z brings it back)", id.value);
        m_selection = {};
    }
}

Result<void> Editor::save()
{
    if (!m_engine.worldSourceFile())
    {
        m_status = "cannot save: the world is not a loose file (archive or no world)";
        return Error{m_status};
    }
    if (!m_generatedEdited.empty() || !m_waynetGeneratedEdited.empty())
    {
        // editor.md, guideline 2: before generated vobs are saved, say what the next generator run undoes.
        std::string ids;
        for (const std::string& name : m_waynetGeneratedEdited)
        {
            ids += (ids.empty() ? "" : ", ") + name;
        }
        for (const u64 id : m_generatedEdited)
        {
            ids += (ids.empty() ? "" : ", ") + std::to_string(id);
            if (ids.size() > 60)
            {
                ids += " ...";
                break;
            }
        }
        ask(std::format("{} generated vob(s) or waynet entries changed ({}): they are saved, but the next "
                        "'gothar-worldgen "
                        "assemble' writes them anew. Save anyway?",
                        m_generatedEdited.size() + m_waynetGeneratedEdited.size(), ids),
            [this] { (void)write(); });
        return Error{"waiting for confirmation"};
    }
    return write();
}

Result<void> Editor::write()
{
    const auto path = m_engine.worldSourceFile();
    if (!path)
    {
        return Error{"no world file"};
    }
    std::error_code ec;
    if (!m_backedUp && std::filesystem::exists(*path, ec))
    {
        // The version before this session's first save stays next to it (undo does not outlive the session).
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
    m_history.markSaved();
    m_generatedEdited.clear();
    m_waynetGeneratedEdited.clear();
    m_status = "saved " + fs::toUtf8(*path);
    if (routinesDirty()) // part E: the routines of the editor's file with the world
    {
        if (auto routines = writeRoutines(); !routines)
        {
            return routines;
        }
        m_status = "saved " + fs::toUtf8(*path) + " and scripts/" + editorRoutineFile();
    }
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
    const bool shiftDown = input.isDown(platform::Key::LeftShift) || input.isDown(platform::Key::RightShift);
    // Play-in-editor (part E): the game runs; F5 or Esc stops it, nothing else of the editor.
    if (playing())
    {
        if (!uiKeyboard && (input.pressed(platform::Key::F5) || input.pressed(platform::Key::Escape)))
        {
            (void)stopPlay();
        }
        return;
    }
    if (!uiKeyboard && input.pressed(platform::Key::F5))
    {
        (void)startPlay(shiftDown); // F5 at the camera, Shift+F5 at the start point (decision 7)
        return;
    }
    drawGhosts();
    if (m_routinePanel.pickPlace && !uiMouse && input.pressed(platform::MouseButton::Left))
    {
        // The routine window waits for a place: the way point or freepoint nearest the click.
        if (std::string name = nearestWaynetName(input.mousePosition()); !name.empty())
        {
            m_routinePanel.at = std::move(name);
            m_routinePanel.pickPlace = false;
        }
        return;
    }
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
        if (input.pressed(platform::Key::G))
        {
            setWaynetMode(!m_waynetMode); // the waynet (M16 part B)
        }
        if (input.pressed(platform::Key::V))
        {
            setZoneMode(!m_zoneMode); // zones (M16 part C)
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
            if (m_waynetMode)
            {
                (void)deleteWaynetSelection();
            }
            else if (m_zoneMode)
            {
                (void)deleteZone();
            }
            else
            {
                deleteSelection();
            }
        }
        if (ctrl && input.pressed(platform::Key::D))
        {
            (void)duplicateSelection();
        }
        if (ctrl && input.pressed(platform::Key::S))
        {
            (void)save();
        }
        const bool shift = input.isDown(platform::Key::LeftShift) || input.isDown(platform::Key::RightShift);
        if (ctrl && input.pressed(platform::Key::Z))
        {
            (void)(shift ? redo() : undo());
        }
        if (ctrl && input.pressed(platform::Key::Y))
        {
            (void)redo();
        }
    }

    const Vec2 mouse = input.mousePosition();
    if (m_waynetMode)
    {
        waynetMouse(engine, mouse, !uiMouse && input.pressed(platform::MouseButton::Left),
                    input.isDown(platform::MouseButton::Left));
        drawWaynet();
        return;
    }
    if (m_zoneMode)
    {
        zoneMouse(engine, mouse, !uiMouse && input.pressed(platform::MouseButton::Left),
                  input.isDown(platform::MouseButton::Left));
        drawZones();
        return;
    }
    drawTriggers();
    placeGizmo();
    const GizmoView v = view();
    if (m_dragHandle != GizmoHandle::None)
    {
        if (input.isDown(platform::MouseButton::Left))
        {
            drag(mouse);
        }
        else
        {
            // The drag is one command: from the vob before it to the vob now.
            m_dragHandle = GizmoHandle::None;
            Command command{std::string(m_gizmo.mode == GizmoMode::Translate ? "move "
                                        : m_gizmo.mode == GizmoMode::Rotate  ? "turn "
                                                                             : "scale ") +
                                vobLabel(m_selection),
                            std::exchange(m_dragBefore, {}),
                            captureVobs(engine.scene(), std::vector{m_selection})};
            if (command.after != command.before)
            {
                if (world::isGenerated(engine.worldFile(), m_selection))
                {
                    m_generatedEdited.insert(m_selection.value);
                }
                m_history.record(std::move(command));
            }
        }
    }
    else if (!uiMouse)
    {
        m_hover = m_selection.valid() ? m_gizmo.hit(v, mouse) : GizmoHandle::None;
        if (input.pressed(platform::MouseButton::Left))
        {
            if (m_hover != GizmoHandle::None)
            {
                if (mayEdit(std::vector{m_selection}, [] {})) // generated: asked first, the next drag goes
                {
                    m_dragBefore = captureVobs(engine.scene(), std::vector{m_selection});
                    m_dragHandle = m_hover;
                    m_dragStart = mouse;
                    m_dragGizmo = m_gizmo;
                    m_dragFrom = worldTransformOf(engine.scene(), engine.scene().findById(m_selection));
                }
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
    setWorldTransform(m_engine.scene(), m_engine.scene().findById(m_selection), t); // one command at the end
    sceneChanged();
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

    drawMobSlots();
    drawGizmo();
}

void Editor::drawGizmo()
{
    // The gizmo, on top of everything.
    render::DebugDraw& draw = m_engine.debugDraw();
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
    p.dirty = dirty();
    p.undoLabel = m_history.undoLabel();
    p.redoLabel = m_history.redoLabel();
    p.confirm = m_confirm;
    p.canSave = m_engine.worldSourceFile().has_value();
    p.status = m_status;
    p.warning = warningFor(m_selection);
    p.gizmoMode = static_cast<i32>(m_gizmo.mode);
    p.localAxes = localAxes;
    p.snap = snap;
    p.snapMove = snapMove;
    p.snapAngle = snapAngle;
    p.assets = m_assets;
    // Part D: lights and mobs.
    p.lightPresets.clear();
    for (const LightPreset& preset : lightPresets())
    {
        p.lightPresets.emplace_back(preset.name);
    }
    const usize windows = m_engine.daylightLights().size();
    p.lightInfo = lightBudgetText(m_engine.frameLights().lights().size(), m_engine.lightBudget().maxLights,
                                  m_engine.worldLightCount() - windows, windows);
    if (!p.hourChanged)
    {
        p.hour = m_engine.gameTime().hourOfDay();
    }
    p.prefabs = prefabNames();
    p.mobIssues.clear();
    for (const MobIssue& issue : m_mobIssues)
    {
        p.mobIssues.push_back(std::format("{} ({}): {}", issue.name, issue.id.value, issue.text));
    }

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
        // Part D: the definition from the list (Lua Mob definitions and mobs.toml types); what it holds is
        // shown - owner, lock, key and contents belong to the Lua definition.
        const std::vector<std::string> names = mobDefinitionChoices(m_mobLists.definitions, m_mobLists.types);
        ChoiceList list = choiceList(mob->definition, names);
        auto& definition = addField("mob definition", Kind::Choice);
        definition.choices = std::move(list.items);
        definition.choice = list.index;
        if (const auto resolved = resolveMob(m_mobLists.definitions, m_mobLists.types, mob->definition))
        {
            for (auto& [label, text] : mobSummary(*resolved))
            {
                addField(std::move(label), Kind::ReadOnly).text = std::move(text);
            }
            if (resolved->type == "door")
            {
                addField("door open", Kind::Flag).flag = mob->open; // stored closed unless set (world.md)
            }
        }
        else
        {
            addField("mob type", Kind::ReadOnly).text = "unknown definition";
        }
    }
    if (const auto* light = scene.get<world::LightSource>(e))
    {
        addField("light colour", Kind::Color).vector = light->color;
        addField("light range", Kind::Number).number = light->range;
        addField("light intensity", Kind::Number).number = light->intensity;
        addField("light flicker", Kind::Number).number = light->flicker;
        addField("light daylight", Kind::Flag).flag = light->daylight; // window light: follows the sky
    }
    if (const auto* sound = scene.get<world::SoundEmitter>(e))
    {
        addField("sound", Kind::Text).text = sound->sound;
        addField("sound range", Kind::Number).number = sound->range;
        addField("sound volume", Kind::Number).number = sound->volume;
    }
    if (const auto* trigger = scene.get<world::TriggerVolume>(e))
    {
        auto& shape = addField("shape", Kind::Choice);
        shape.choices = {"box", "sphere"};
        shape.choice = trigger->shape == world::TriggerVolume::Shape::Box ? 0 : 1;
        if (trigger->shape == world::TriggerVolume::Shape::Box)
        {
            addField("half extents", Kind::Vector).vector = trigger->halfExtents;
        }
        else
        {
            addField("radius", Kind::Number).number = trigger->radius;
        }
        auto& filter = addField("filter", Kind::Choice);
        filter.choices = {"player", "npc", "any"};
        filter.choice = static_cast<i32>(trigger->filter);
        // Part C: names from lists - the loaded scripts' functions, the worlds, the target world's starts.
        const auto choose =
            [&](std::string label, std::string_view current, std::span<const std::string> known)
        {
            ChoiceList list = choiceList(current, known);
            auto& field = addField(std::move(label), Kind::Choice);
            field.choices = std::move(list.items);
            field.choice = list.index;
        };
        choose("on enter", trigger->onEnter, m_scriptFunctions);
        choose("on leave", trigger->onLeave, m_scriptFunctions);
        addField("once", Kind::Flag).flag = trigger->once;
        choose("change world", trigger->changeWorld, m_worlds);
        const auto starts = startsOf(trigger->changeWorld).value_or(std::vector<std::string>{});
        choose("change start", trigger->changeStart, starts);
        addField("owner", Kind::Text).text = trigger->owner; // private area (M9): Npc instance or guild
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
    if (p.hourChanged)
    {
        setPreviewHour(p.hour);
    }
    if (p.addLight && p.lightPreset >= 0 && static_cast<usize>(p.lightPreset) < lightPresets().size())
    {
        (void)placeLight(lightPresets()[static_cast<usize>(p.lightPreset)].name);
    }
    if (p.savePrefab)
    {
        (void)savePrefab(p.prefabName);
    }
    if (p.placePrefab >= 0 && static_cast<usize>(p.placePrefab) < p.prefabs.size())
    {
        (void)placePrefab(p.prefabs[static_cast<usize>(p.placePrefab)]);
    }
    if (p.checkMobs)
    {
        (void)checkMobs();
    }
    if (p.clickedMobIssue >= 0 && static_cast<usize>(p.clickedMobIssue) < m_mobIssues.size())
    {
        select(m_mobIssues[static_cast<usize>(p.clickedMobIssue)].id);
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
    if (p.undo)
    {
        (void)undo();
    }
    if (p.redo)
    {
        (void)redo();
    }
    if (p.confirmYes || p.confirmNo)
    {
        confirm(p.confirmYes);
    }
    if (p.remove)
    {
        deleteSelection();
        return;
    }

    // Changed fields: one command; typing on in the same fields of the same vob joins it.
    std::string labels;
    for (const ui::EditorField& f : p.fields)
    {
        if (f.changed)
        {
            labels += (labels.empty() ? "" : ", ") + f.label;
        }
    }
    if (labels.empty() || p.clickedVob != 0 ||
        !m_engine.scene().valid(m_engine.scene().findById(m_selection)))
    {
        return;
    }
    (void)edit(
        "edit " + labels + " of " + vobLabel(m_selection), {m_selection},
        [this, fields = p.fields]
        {
            applyFields(fields);
            return Result<void>{};
        },
        true);
}

void Editor::applyFields(const std::vector<ui::EditorField>& fields)
{
    world::Scene& scene = m_engine.scene();
    const entt::entity e = scene.findById(m_selection);
    if (!scene.valid(e))
    {
        return;
    }
    Transform t = worldTransformOf(scene, e);
    bool moved = false;
    for (const ui::EditorField& f : fields)
    {
        if (!f.changed)
        {
            continue;
        }
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
            // From the list only ("-" keeps the definition: a mob vob needs one).
            if (std::string chosen = choiceValue(f.choices, f.choice); !chosen.empty())
            {
                world::MobRef* mob = scene.get<world::MobRef>(e);
                mob->definition = std::move(chosen);
                const auto resolved = resolveMob(m_mobLists.definitions, m_mobLists.types, mob->definition);
                mob->open = mob->open && resolved && resolved->type == "door"; // only doors are stored open
            }
        }
        else if (f.label == "door open")
        {
            scene.get<world::MobRef>(e)->open = f.flag;
        }
        else if (auto* light = scene.get<world::LightSource>(e); light && f.label.starts_with("light "))
        {
            light->color = f.label == "light colour" ? f.vector : light->color;
            light->range = f.label == "light range" ? std::max(f.number, 0.1f) : light->range;
            light->intensity = f.label == "light intensity" ? std::max(f.number, 0.0f) : light->intensity;
            light->flicker = f.label == "light flicker" ? std::clamp(f.number, 0.0f, 1.0f) : light->flicker;
            light->daylight = f.label == "light daylight" ? f.flag : light->daylight;
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
            else if (f.label == "shape")
            {
                trigger->shape =
                    f.choice == 1 ? world::TriggerVolume::Shape::Sphere : world::TriggerVolume::Shape::Box;
            }
            else if (f.label == "filter")
            {
                trigger->filter = static_cast<world::TriggerVolume::Filter>(std::clamp(f.choice, 0, 2));
            }
            else if (f.label == "on enter")
            {
                trigger->onEnter = choiceValue(f.choices, f.choice);
            }
            else if (f.label == "on leave")
            {
                trigger->onLeave = choiceValue(f.choices, f.choice);
            }
            else if (f.label == "once")
            {
                trigger->once = f.flag;
            }
            else if (f.label == "change world")
            {
                trigger->changeWorld = choiceValue(f.choices, f.choice);
            }
            else if (f.label == "owner")
            {
                trigger->owner = f.text;
            }
            else if (f.label == "change start")
            {
                trigger->changeStart = choiceValue(f.choices, f.choice);
            }
        }
    }
    if (moved)
    {
        setWorldTransform(scene, e, t);
    }
}

void Editor::ui(Engine&, ui::DebugUi& debugUi)
{
    if (!playing()) // while playing only the routine window (its stop button)
    {
        fillPanel();
        debugUi.editorPanel(m_panel);
        applyPanel();
        fillWaynetPanel();
        debugUi.waynetPanel(m_waynetPanel);
        applyWaynetPanel();
        fillZonePanel();
        debugUi.zonePanel(m_zonePanel);
        applyZonePanel();
    }
    fillRoutinePanel();
    debugUi.routinePanel(m_routinePanel);
    applyRoutinePanel();
}
} // namespace g7::editor

// The editor's waynet mode (M16 part B, docs/modules/editor.md): select, add, move, join, rename and delete
// way points and freepoints as undoable commands; the checks of world.md; a path probe. Contract (world.md
// "Wegnetz"): moved or renamed generated points become the human's; deleting a generated one lasts until the
// next generator run - the editor shows the line for welt's annotations (decision 2) instead of writing
// welt's file.

#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/editor/Editor.hpp>
#include <g7/physics/Physics.hpp>
#include <g7/platform/Input.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/DebugUi.hpp>
#include <g7/world/Terrain.hpp>

#include <algorithm>
#include <format>
#include <numbers>

namespace g7::editor
{
namespace
{
constexpr f32 kLabelRadius = 25.0f;   ///< names this near
constexpr f32 kPickPixels = 12.0f;    ///< a click this near a point's screen position selects it
constexpr f32 kWalkableNormal = 0.7f; ///< ground facing up at least this much is walkable
const std::vector<std::string> kFreepointTypes = {
    "SIT",   "STAND", "SMALLTALK", "ROAM",  "SLEEP",  "CAMPFIRE", "LEAN",   "SWEEP", "DRINK",
    "TRAIN", "CHOP",  "HARVEST",   "WATER", "REPAIR", "PRAY",     "PREACH", "BEG",   "MUSIC"};

Vec3 facing(f32 yawDegrees)
{
    const f32 yaw = glm::radians(yawDegrees);
    return Vec3(-std::sin(yaw), 0.0f, -std::cos(yaw)); // yaw 0 = -Z, positive turns left (as the movement)
}

f32 yawOf(const Vec3& dir)
{
    return glm::degrees(std::atan2(-dir.x, -dir.z));
}

const world::WaynetPoint* entry(const world::WaynetData& w, std::string_view name)
{
    const world::WaynetPoint* p = w.findPoint(name);
    return p != nullptr ? p : w.findFreepoint(name);
}
} // namespace

void Editor::setWaynetMode(bool on)
{
    m_waynetMode = on;
    if (on)
    {
        select({});
    }
    else
    {
        m_waynetSelected.clear();
    }
}

void Editor::selectWaynet(std::string name)
{
    m_waynetSelected = entry(m_waynetData, name) != nullptr ? std::move(name) : std::string();
    m_annotation.clear();
}

Result<void> Editor::editWaynet(std::string label, std::vector<std::string> names,
                                std::vector<std::pair<std::string, std::string>> edges,
                                const std::function<Result<void>(world::WaynetData&)>& change, bool mergeable)
{
    // Generated entries ask first, once per session (as generated vobs do).
    if (!m_waynetConfirmed)
    {
        for (const std::string& name : names)
        {
            if (isGeneratedEntry(m_waynetData, name))
            {
                ask(std::format(
                        "{} is generated (owner worldgen): moving or renaming makes it yours; deleting "
                        "lasts only until the next generator run. Edit generated waynet entries anyway?",
                        name),
                    [this, label, names, edges, change, mergeable]
                    {
                        m_waynetConfirmed = true;
                        (void)editWaynet(label, names, edges, change, mergeable);
                    });
                return Error{"waiting for confirmation"};
            }
        }
    }
    Command command{std::move(label), {}, {}, mergeable};
    command.waynetBefore = captureWaynet(m_waynetData, names, edges);
    world::WaynetData edited = m_waynetData;
    if (auto changed = change(edited); !changed)
    {
        m_status = changed.error().message;
        return changed;
    }
    if (auto handed = m_engine.setWaynet(edited); !handed)
    {
        m_status = handed.error().message;
        return handed;
    }
    for (const std::string& name : names)
    {
        if (isGeneratedEntry(m_waynetData, name))
        {
            m_waynetGeneratedEdited.insert(name);
        }
    }
    m_waynetData = std::move(edited);
    command.waynetAfter =
        captureWaynet(m_waynetData, command.waynetBefore->names, command.waynetBefore->edges);
    m_history.record(std::move(command));
    return {};
}

Result<std::string> Editor::addWaypoint(const Vec3& feet)
{
    const std::string from =
        m_waynetData.findPoint(m_waynetSelected) != nullptr ? m_waynetSelected : std::string();
    std::string stem = !from.empty() ? nameStem(from) : std::string();
    if (stem.empty())
    {
        stem = toUpper(fs::Path(m_engine.worldPath()).stem().string());
    }
    std::string name = suggestName(m_waynetData, "WP_", stem);
    std::vector<std::pair<std::string, std::string>> edges;
    if (!from.empty())
    {
        edges.push_back(edgeKey(from, name));
    }
    if (auto added = editWaynet(std::format("add {}", name), {name}, edges,
                                [&](world::WaynetData& w) -> Result<void>
                                {
                                    if (auto a = editor::addPoint(w, name, feet); !a)
                                    {
                                        return a;
                                    }
                                    if (!from.empty())
                                    {
                                        if (auto c = editor::connect(w, from, name); !c)
                                        {
                                            return c.error();
                                        }
                                    }
                                    return {};
                                });
        !added)
    {
        return added.error();
    }
    selectWaynet(name);
    m_status =
        from.empty() ? std::format("added {}", name) : std::format("added {} joined to {}", name, from);
    return name;
}

Result<std::string> Editor::addFreepoint(std::string_view type, const Vec3& feet, f32 yawDegrees)
{
    std::string stem = !m_waynetSelected.empty() ? nameStem(m_waynetSelected) : std::string();
    if (stem.empty())
    {
        stem = toUpper(fs::Path(m_engine.worldPath()).stem().string());
    }
    std::string name = suggestName(m_waynetData, std::format("FP_{}_", type), stem);
    const Vec3 dir = facing(yawDegrees);
    if (auto added = editWaynet(std::format("add {}", name), {name}, {}, [&](world::WaynetData& w)
                                { return editor::addFreepoint(w, name, feet, dir); });
        !added)
    {
        return added.error();
    }
    selectWaynet(name);
    m_status = std::format("added {}", name);
    return name;
}

Result<void> Editor::moveWaynetSelection(const Vec3& feet, std::optional<f32> yawDegrees)
{
    if (m_waynetSelected.empty())
    {
        return Error{"nothing selected"};
    }
    const std::string name = m_waynetSelected;
    const std::optional<Vec3> dir = yawDegrees ? std::optional<Vec3>(facing(*yawDegrees)) : std::nullopt;
    return editWaynet(
        std::format("move {}", name), {name}, {},
        [name, feet, dir](world::WaynetData& w) { return editor::moveEntry(w, name, feet, dir); }, true);
}

Result<void> Editor::renameWaynetSelection(std::string to)
{
    if (m_waynetSelected.empty())
    {
        return Error{"nothing selected"};
    }
    const std::string from = m_waynetSelected;
    if (auto renamed =
            editWaynet(std::format("rename {} to {}", from, to), {from, to}, {},
                       [from, to](world::WaynetData& w) { return editor::renameEntry(w, from, to); });
        !renamed)
    {
        return renamed;
    }
    selectWaynet(to);
    return {};
}

Result<void> Editor::deleteWaynetSelection()
{
    if (m_waynetSelected.empty())
    {
        return Error{"nothing selected"};
    }
    const std::string name = m_waynetSelected;
    const bool generated = isGeneratedEntry(m_waynetData, name);
    if (auto removed = editWaynet(std::format("delete {}", name), {name}, {},
                                  [name](world::WaynetData& w) { return editor::removeEntry(w, name); });
        !removed)
    {
        return removed;
    }
    m_waynetSelected.clear();
    // Decision 2: the editor does not write welt's annotations; it shows the line.
    m_annotation = generated ? removalAnnotation(name) : std::string();
    m_status =
        generated ? std::format("deleted {} until the next generator run - for good: welt's annotation", name)
                  : std::format("deleted {}", name);
    return {};
}

Result<bool> Editor::toggleEdge(std::string_view a, std::string_view b)
{
    const auto key = edgeKey(a, b);
    const bool joined = std::ranges::any_of(m_waynetData.edges, [&](const world::WaynetEdge& e)
                                            { return edgeKey(e.a, e.b) == key; });
    bool now = false;
    if (auto toggled =
            editWaynet(std::format("{} {} - {}", joined ? "part" : "join", key.first, key.second), {}, {key},
                       [&](world::WaynetData& w) -> Result<void>
                       {
                           auto r = joined ? editor::disconnect(w, key.first, key.second)
                                           : editor::connect(w, key.first, key.second);
                           if (!r)
                           {
                               return r.error();
                           }
                           now = !joined;
                           return {};
                       });
        !toggled)
    {
        return toggled.error();
    }
    return now;
}

const std::vector<WaynetIssue>& Editor::checkWaynet()
{
    const physics::PhysicsWorld& physics = m_engine.physics();
    const auto blocked = [&](const Vec3& from, const Vec3& to)
    {
        const Vec3 d = to - from;
        const f32 length = glm::length(d);
        return physics.valid() && length > 1e-3f &&
               physics.raycast(from, d / length, length, physics::layerBit(physics::Layer::World))
                   .has_value();
    };
    const auto ground = [&](const Vec3& feet)
    {
        if (!physics.valid())
        {
            return true;
        }
        const auto hit = physics.raycast(feet + Vec3(0.0f, 0.5f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 1.0f,
                                         physics::layerBit(physics::Layer::World));
        return hit.has_value() && hit->normal.y >= kWalkableNormal;
    };
    m_issues = editor::checkWaynet(m_waynetData, {}, blocked, ground);
    m_status = std::format("waynet: {} issue(s)", m_issues.size());
    return m_issues;
}

std::optional<std::vector<std::string>> Editor::probePath(std::string_view from, std::string_view to)
{
    m_probe.clear();
    const ai::Waynet& net = m_engine.waynet();
    const auto a = net.find(from);
    const auto b = net.find(to);
    if (!a || !b)
    {
        return std::nullopt;
    }
    const auto path = net.path(*a, *b);
    if (!path)
    {
        return std::nullopt;
    }
    std::vector<std::string> names;
    for (const u32 i : *path)
    {
        names.push_back(net.points()[i].name);
        m_probe.push_back(net.points()[i].position);
    }
    return names;
}

std::optional<Vec3> Editor::groundUnder(const Vec2& mouse) const
{
    const Ray ray = screenRay(view(), mouse);
    if (const auto hit = m_engine.physics().valid()
                             ? m_engine.physics().raycast(ray.origin, ray.direction, 500.0f,
                                                          physics::layerBit(physics::Layer::World))
                             : std::nullopt)
    {
        return hit->position;
    }
    if (std::abs(ray.direction.y) > 1e-4f) // no collision (tests, empty worlds): the plane y = 0
    {
        const f32 t = -ray.origin.y / ray.direction.y;
        if (t > 0.0f)
        {
            return ray.origin + ray.direction * t;
        }
    }
    return std::nullopt;
}

void Editor::waynetMouse(Engine& engine, const Vec2& mouse, bool pressed, bool held)
{
    const platform::Input& input = engine.input();
    const bool ctrl = input.isDown(platform::Key::LeftCtrl) || input.isDown(platform::Key::RightCtrl);
    const bool shift = input.isDown(platform::Key::LeftShift) || input.isDown(platform::Key::RightShift);
    const GizmoView v = view();
    const world::WaynetPoint* selected = entry(m_waynetData, m_waynetSelected);
    // Dragging the selection with the gizmo (move only): one command at the end.
    if (m_waynetDragging)
    {
        if (held && selected != nullptr)
        {
            const Vec3 delta = m_dragGizmo.dragTranslate(m_dragHandle, v, m_dragStart, mouse);
            Vec3 to = m_dragFrom.position + delta;
            if (snap)
            {
                to = Vec3(g7::editor::snap(to.x, snapMove), to.y, g7::editor::snap(to.z, snapMove));
            }
            world::WaynetData moved = m_waynetData;
            if (editor::moveEntry(moved, m_waynetSelected, to) && m_engine.setWaynet(moved))
            {
                m_waynetData = std::move(moved);
            }
            return;
        }
        m_waynetDragging = false;
        Command command{std::format("move {}", m_waynetSelected), {}, {}, false};
        command.waynetBefore = std::exchange(m_waynetDragBefore, std::nullopt);
        command.waynetAfter =
            captureWaynet(m_waynetData, command.waynetBefore->names, command.waynetBefore->edges);
        if (command.waynetAfter != command.waynetBefore)
        {
            m_history.record(std::move(command));
        }
        return;
    }
    if (selected != nullptr)
    {
        m_gizmo.mode = GizmoMode::Translate;
        m_gizmo.origin = selected->position;
        m_gizmo.axes = Mat3(1.0f);
        m_hover = m_gizmo.hit(v, mouse);
    }
    if (!pressed)
    {
        return;
    }
    if (selected != nullptr && m_hover != GizmoHandle::None && !ctrl && !shift)
    {
        if (!m_waynetConfirmed && isGeneratedEntry(m_waynetData, m_waynetSelected))
        {
            ask(std::format(
                    "{} is generated (owner worldgen): moving it makes it yours. Edit generated waynet "
                    "entries anyway?",
                    m_waynetSelected),
                [this] { m_waynetConfirmed = true; });
            return;
        }
        m_waynetDragging = true;
        m_waynetDragBefore = captureWaynet(m_waynetData, std::vector{m_waynetSelected});
        m_dragHandle = m_hover;
        m_dragStart = mouse;
        m_dragGizmo = m_gizmo;
        m_dragFrom = Transform{};
        m_dragFrom.position = selected->position;
        return;
    }
    // The point nearest the click on the screen.
    std::string nearest;
    f32 best = kPickPixels;
    for (const auto* list : {&m_waynetData.points, &m_waynetData.freepoints})
    {
        for (const world::WaynetPoint& p : *list)
        {
            if (glm::length(p.position - v.eye) > m_waynetRadius)
            {
                continue;
            }
            if (const auto screen = project(v, p.position); screen && glm::length(*screen - mouse) < best)
            {
                best = glm::length(*screen - mouse);
                nearest = p.name;
            }
        }
    }
    if (shift && !nearest.empty() && m_waynetData.findPoint(m_waynetSelected) != nullptr &&
        m_waynetData.findPoint(nearest) != nullptr && nearest != m_waynetSelected)
    {
        (void)toggleEdge(m_waynetSelected, nearest);
        return;
    }
    if (ctrl && nearest.empty())
    {
        if (const auto ground = groundUnder(mouse))
        {
            (void)addWaypoint(*ground);
        }
        return;
    }
    selectWaynet(nearest);
}

void Editor::drawWaynet()
{
    render::DebugDraw& draw = m_engine.debugDraw();
    const Vec3 eye = m_engine.camera().transform.position;
    const Vec3 lift(0.0f, 0.1f, 0.0f);
    const Vec4 own(0.3f, 0.85f, 1.0f, 1.0f);
    const Vec4 generated(0.45f, 0.55f, 0.65f, 0.8f); // paler: the generator's
    const Vec4 freeOwn(1.0f, 0.8f, 0.25f, 1.0f);
    const Vec4 freeGenerated(0.75f, 0.65f, 0.4f, 0.8f);
    const auto near = [&](const Vec3& p) { return glm::length(p - eye) <= m_waynetRadius; };
    for (const world::WaynetEdge& e : m_waynetData.edges)
    {
        const world::WaynetPoint* a = m_waynetData.findPoint(e.a);
        const world::WaynetPoint* b = m_waynetData.findPoint(e.b);
        if (a != nullptr && b != nullptr && (near(a->position) || near(b->position)))
        {
            draw.line(a->position + lift, b->position + lift, {e.generated ? generated : own});
        }
    }
    for (const world::WaynetPoint& p : m_waynetData.points)
    {
        if (!near(p.position))
        {
            continue;
        }
        const bool sel = p.name == m_waynetSelected;
        const Vec4 c = sel ? Vec4(1.0f, 0.95f, 0.2f, 1.0f) : p.generated ? generated : own;
        draw.sphere(p.position + Vec3(0.0f, 0.25f, 0.0f), sel ? 0.3f : 0.2f, {c});
        if (glm::length(p.position - eye) < kLabelRadius)
        {
            draw.text(p.position + Vec3(0.0f, 0.7f, 0.0f), p.name, {c});
        }
    }
    for (const world::WaynetPoint& p : m_waynetData.freepoints)
    {
        if (!near(p.position))
        {
            continue;
        }
        const bool sel = p.name == m_waynetSelected;
        const Vec4 c = sel ? Vec4(1.0f, 0.95f, 0.2f, 1.0f) : p.generated ? freeGenerated : freeOwn;
        draw.circle(p.position + lift, Vec3(0.0f, 1.0f, 0.0f), 0.3f, {c});
        draw.arrow(p.position + lift, p.position + lift + p.dir.value_or(Vec3(0.0f, 0.0f, -1.0f)) * 0.7f,
                   {c});
        if (glm::length(p.position - eye) < kLabelRadius)
        {
            draw.text(p.position + Vec3(0.0f, 0.5f, 0.0f), p.name, {c});
        }
    }
    for (const WaynetIssue& issue : m_issues)
    {
        if (near(issue.position))
        {
            draw.cross(issue.position + Vec3(0.0f, 0.4f, 0.0f), 0.5f, {Vec4(1.0f, 0.25f, 0.2f, 1.0f)});
        }
    }
    for (usize i = 1; i < m_probe.size(); ++i)
    {
        draw.line(m_probe[i - 1] + Vec3(0.0f, 0.3f, 0.0f), m_probe[i] + Vec3(0.0f, 0.3f, 0.0f),
                  {Vec4(0.4f, 1.0f, 0.4f, 1.0f), 0.0f, false});
    }
    if (const world::WaynetPoint* p = entry(m_waynetData, m_waynetSelected))
    {
        const GizmoView v = view();
        const f32 arm = m_gizmo.armLength(v);
        constexpr GizmoHandle kAxes[3] = {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};
        const Vec4 colours[3] = {Vec4(0.95f, 0.25f, 0.25f, 1.0f), Vec4(0.3f, 0.9f, 0.3f, 1.0f),
                                 Vec4(0.3f, 0.5f, 1.0f, 1.0f)};
        for (int a = 0; a < 3; ++a)
        {
            const bool hot = m_hover == kAxes[a] || (m_waynetDragging && m_dragHandle == kAxes[a]);
            draw.arrow(p->position, p->position + Mat3(1.0f)[a] * arm,
                       {hot ? Vec4(1.0f, 0.9f, 0.2f, 1.0f) : colours[a], 0.0f, false});
        }
    }
}

void Editor::fillWaynetPanel()
{
    ui::WaynetPanel& p = m_waynetPanel;
    p.shown = true;
    p.active = m_waynetMode;
    p.freepointTypes = kFreepointTypes;
    p.annotation = m_annotation;
    p.issues.clear();
    for (const WaynetIssue& issue : m_issues)
    {
        p.issues.push_back(issue.text);
    }
    const world::WaynetPoint* sel = entry(m_waynetData, m_waynetSelected);
    if (p.selected != m_waynetSelected)
    {
        p.name = m_waynetSelected; // a new selection: its name in the rename field
    }
    p.selected = m_waynetSelected;
    p.selectedFreepoint = m_waynetData.findFreepoint(m_waynetSelected) != nullptr;
    p.selectedGenerated = sel != nullptr && sel->generated;
    if (sel != nullptr)
    {
        p.position = sel->position;
        p.yawDegrees = sel->dir ? yawOf(*sel->dir) : 0.0f;
    }
}

void Editor::applyWaynetPanel()
{
    ui::WaynetPanel& p = m_waynetPanel;
    if (p.active != m_waynetMode)
    {
        setWaynetMode(p.active);
    }
    const Vec3 ahead = [&]
    {
        const render::Camera& camera = m_engine.camera();
        const Vec3 f = camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
        Vec3 at = camera.transform.position + glm::normalize(Vec3(f.x, 0.0f, f.z)) * 4.0f;
        at.y = m_engine.terrain() != nullptr ? m_engine.terrain()->heightAt(at.x, at.z) : at.y - 1.6f;
        return at;
    }();
    if (p.addPoint)
    {
        (void)addWaypoint(ahead);
    }
    if (p.addFreepoint && p.type >= 0 && static_cast<usize>(p.type) < kFreepointTypes.size())
    {
        (void)addFreepoint(kFreepointTypes[static_cast<usize>(p.type)], ahead, 0.0f);
    }
    if (p.moved && !m_waynetSelected.empty())
    {
        (void)moveWaynetSelection(p.position,
                                  p.selectedFreepoint ? std::optional<f32>(p.yawDegrees) : std::nullopt);
    }
    if (p.rename && !p.name.empty() && p.name != m_waynetSelected)
    {
        (void)renameWaynetSelection(p.name);
    }
    if (p.remove)
    {
        (void)deleteWaynetSelection();
    }
    if (p.check)
    {
        (void)checkWaynet();
    }
    if (p.clickedIssue >= 0 && static_cast<usize>(p.clickedIssue) < m_issues.size())
    {
        const WaynetIssue& issue = m_issues[static_cast<usize>(p.clickedIssue)];
        selectWaynet(issue.name);
        // The camera goes there: a few metres back along its view, looking on.
        render::Camera& camera = m_engine.camera();
        const Vec3 back = camera.transform.rotation * Vec3(0.0f, 0.0f, 6.0f);
        camera.transform.position = issue.position + back + Vec3(0.0f, 2.0f, 0.0f);
    }
    if (p.probe)
    {
        const auto path = probePath(p.pathFrom, p.pathTo);
        p.pathResult = path ? std::format("{} points, {} -> {}", path->size(), p.pathFrom, p.pathTo)
                            : std::string("no path (unknown point or not connected)");
    }
}
} // namespace g7::editor

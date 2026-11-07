// The editor's routines and play-in-editor (M16 part E, docs/modules/editor.md): ghosts where the routines
// put the NPCs at a preview hour (decision 8: a marker with name and state, the selected NPC's routine as a
// simple figure), the day plan checks, editing the routines of the editor's own Lua file per site (decision
// 4) and starting the simulation at the camera (decision 7: F5; Shift+F5 at the start point).

#include <g7/editor/Editor.hpp>
#include <g7/physics/Physics.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/DebugUi.hpp>

#include <algorithm>
#include <format>

namespace g7::editor
{
namespace
{
constexpr f32 kGhostRadius = 150.0f; ///< m: ghosts this near the camera are drawn
constexpr f32 kPickPixels = 12.0f;
} // namespace

std::string Editor::editorRoutineFile() const
{
    return editor::editorRoutineFile(siteOf(m_engine.worldPath()));
}

std::vector<RoutineInfo> Editor::allRoutines() const
{
    const std::string file = editorRoutineFile();
    std::vector<RoutineInfo> all;
    for (RoutineInfo& r : m_engine.routines())
    {
        if (r.file != file)
        {
            all.push_back(std::move(r)); // hand-written: shown only
        }
    }
    all.insert(all.end(), m_routines.begin(), m_routines.end());
    std::ranges::sort(all, {}, &RoutineInfo::name);
    return all;
}

std::vector<Editor::Ghost> Editor::ghosts(u32 minute) const
{
    const std::vector<RoutineInfo> routines = allRoutines();
    std::vector<Ghost> out;
    for (const NpcRoutineInfo& npc : m_engine.npcRoutines())
    {
        Ghost g;
        g.npc = npc.npc;
        g.routine = npc.routine;
        g.now = npc.position;
        const auto r = std::ranges::find(routines, npc.routine, &RoutineInfo::name);
        if (r != routines.end())
        {
            if (const auto entry = entryAt(*r, minute))
            {
                g.state = r->entries[*entry].state;
                g.at = r->entries[*entry].at;
                g.place = m_engine.routinePlace(g.at);
            }
        }
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<RoutineIssue> Editor::checkRoutines() const
{
    std::vector<RoutineIssue> issues;
    const PlaceKnownFn known = [this](std::string_view at) { return m_engine.routinePlace(at).has_value(); };
    // The routines of this world: those its NPCs follow and those of its editor file (other sites' routines
    // name places of other waynets).
    std::vector<std::string> used;
    for (const NpcRoutineInfo& npc : m_engine.npcRoutines())
    {
        used.push_back(npc.routine);
    }
    for (const RoutineInfo& r : allRoutines())
    {
        const bool ours = std::ranges::find(used, r.name) != used.end() ||
                          std::ranges::find(m_routines, r.name, &RoutineInfo::name) != m_routines.end();
        if (!ours)
        {
            continue;
        }
        for (RoutineIssue& issue : checkRoutine(r, known))
        {
            issues.push_back(std::move(issue));
        }
    }
    return issues;
}

Result<void> Editor::editRoutines(std::string label,
                                  const std::function<Result<void>(std::vector<RoutineInfo>&)>& change)
{
    std::vector<RoutineInfo> edited = m_routines;
    if (auto changed = change(edited); !changed)
    {
        m_status = changed.error().message;
        return changed;
    }
    for (RoutineInfo& r : edited)
    {
        sortEntries(r);
    }
    // Only what the file can hold: the text must read back the same.
    const std::string site = siteOf(m_engine.worldPath());
    auto back = parseRoutines(editor::writeRoutines(edited, site), editorRoutineFile());
    if (!back)
    {
        m_status = back.error().message;
        return back.error();
    }
    Command command{std::move(label), {}, {}, false};
    command.routinesBefore = std::exchange(m_routines, std::move(edited));
    command.routinesAfter = m_routines;
    m_history.record(std::move(command));
    return {};
}

Result<void> Editor::newRoutine(std::string name)
{
    if (!validRoutineName(name))
    {
        return Error{std::format("'{}': a routine name is rtn_ and lower-case letters, digits, '_'", name)};
    }
    const std::vector<RoutineInfo> all = allRoutines();
    if (std::ranges::find(all, name, &RoutineInfo::name) != all.end())
    {
        return Error{std::format("there is a routine '{}' already", name)};
    }
    const std::string file = editorRoutineFile();
    if (auto made = editRoutines(std::format("new routine {}", name),
                                 [&](std::vector<RoutineInfo>& routines) -> Result<void>
                                 {
                                     routines.push_back({name, file, {}});
                                     return {};
                                 });
        !made)
    {
        return made;
    }
    m_selectedRoutine = name;
    return {};
}

Result<void> Editor::addRoutineEntry(std::string_view routine, RoutineEntryInfo entry)
{
    return editRoutines(std::format("{}: add {}-{}", routine, entry.from, entry.to),
                        [&](std::vector<RoutineInfo>& routines) -> Result<void>
                        {
                            const auto r = std::ranges::find(routines, routine, &RoutineInfo::name);
                            if (r == routines.end())
                            {
                                return Error{
                                    std::format("'{}' is not in the editor's file - shown only", routine)};
                            }
                            r->entries.push_back(std::move(entry));
                            return {};
                        });
}

Result<void> Editor::setRoutineEntry(std::string_view routine, usize index, RoutineEntryInfo entry)
{
    return editRoutines(
        std::format("{}: change entry {}", routine, index + 1),
        [&](std::vector<RoutineInfo>& routines) -> Result<void>
        {
            const auto r = std::ranges::find(routines, routine, &RoutineInfo::name);
            if (r == routines.end() || index >= r->entries.size())
            {
                return Error{std::format("'{}' has no entry {} in the editor's file", routine, index + 1)};
            }
            r->entries[index] = std::move(entry);
            return {};
        });
}

Result<void> Editor::removeRoutineEntry(std::string_view routine, usize index)
{
    return editRoutines(
        std::format("{}: remove entry {}", routine, index + 1),
        [&](std::vector<RoutineInfo>& routines) -> Result<void>
        {
            const auto r = std::ranges::find(routines, routine, &RoutineInfo::name);
            if (r == routines.end() || index >= r->entries.size())
            {
                return Error{std::format("'{}' has no entry {} in the editor's file", routine, index + 1)};
            }
            r->entries.erase(r->entries.begin() + static_cast<std::ptrdiff_t>(index));
            return {};
        });
}

Result<void> Editor::writeRoutines()
{
    const std::string file = editorRoutineFile();
    if (auto written =
            m_engine.writeScript(file, editor::writeRoutines(m_routines, siteOf(m_engine.worldPath())));
        !written)
    {
        m_status = written.error().message;
        return written;
    }
    m_routinesWritten = m_routines;
    m_status = std::format("wrote scripts/{} ({} routine(s)), scripts reloaded", file, m_routines.size());
    return {};
}

bool Editor::playing() const noexcept
{
    return m_engine.playing();
}

Result<void> Editor::startPlay(bool atStartPoint)
{
    (void)m_engine.setGhostFigure(std::nullopt); // the game has its own NPCs
    m_playCamera = m_engine.camera().transform;
    std::optional<Transform> at;
    if (!atStartPoint)
    {
        // The hero's feet on the ground under the camera, facing as it looks.
        Transform feet = m_playCamera;
        const physics::PhysicsWorld& physics = m_engine.physics();
        const auto ground = physics.valid() ? physics.raycast(feet.position, Vec3(0.0f, -1.0f, 0.0f), 200.0f,
                                                              physics::layerBit(physics::Layer::World))
                                            : std::nullopt;
        feet.position =
            ground ? ground->position + Vec3(0.0f, 0.05f, 0.0f) : feet.position - Vec3(0.0f, 1.7f, 0.0f);
        at = feet;
    }
    setWaynetMode(false);
    setZoneMode(false);
    select({});
    if (auto started = m_engine.startPlay(at); !started)
    {
        m_status = started.error().message;
        return started;
    }
    m_status =
        atStartPoint ? "playing from the start point (F5: stop)" : "playing from the camera (F5: stop)";
    return {};
}

Result<void> Editor::stopPlay()
{
    auto stopped = m_engine.stopPlay(m_playCamera);
    // The kept game came from the file and its delta: the editor's waynet and zones go back to the engine.
    if (auto w = m_engine.setWaynet(m_waynetData); !w)
    {
        m_status = w.error().message;
    }
    if (auto z = m_engine.setZones(m_zones); !z)
    {
        m_status = z.error().message;
    }
    sceneChanged();
    if (!stopped)
    {
        m_status = stopped.error().message;
        return stopped;
    }
    m_status = "stopped - the world as before the start";
    return {};
}

std::string Editor::nearestWaynetName(const Vec2& mouse) const
{
    const GizmoView v = view();
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
    return nearest;
}

void Editor::drawGhosts()
{
    if (!m_routinePanel.ghosts)
    {
        (void)m_engine.setGhostFigure(std::nullopt);
        return;
    }
    render::DebugDraw& draw = m_engine.debugDraw();
    const Vec3 eye = m_engine.camera().transform.position;
    const u32 minute = static_cast<u32>(std::clamp(m_routinePanel.hour, 0.0f, 24.0f) * 60.0f) % 1440u;
    bool figureShown = false;
    for (const Ghost& g : ghosts(minute))
    {
        if (!g.place || glm::length(*g.place - eye) > kGhostRadius)
        {
            continue;
        }
        const bool selected = g.routine == m_selectedRoutine && !g.routine.empty();
        const Vec4 colour = selected ? Vec4(1.0f, 0.9f, 0.3f, 1.0f) : Vec4(0.7f, 0.85f, 1.0f, 0.8f);
        const Vec3 p = *g.place;
        // Decision 8: the selected routine's NPC as its real figure at rest (the first such NPC), the others
        // as markers. Facing the camera, so the face can be judged.
        if (selected && !figureShown)
        {
            const Vec3 look = eye - p;
            const f32 yaw = std::atan2(-look.x, -look.z); // yaw 0 looks along -Z, positive turns left
            figureShown = m_engine.setGhostFigure(
                Engine::GhostFigure{g.npc.substr(0, g.npc.find('#')), p, yaw}); // "npc#2": its instance
        }
        if (!(selected && figureShown))
        {
            draw.circle(p + Vec3(0.0f, 0.03f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.35f, {colour});
            draw.line(p, p + Vec3(0.0f, 1.8f, 0.0f), {colour});
        }
        draw.text(p + Vec3(0.0f, 2.0f, 0.0f), std::format("{}: {}", g.npc, g.state), {colour});
        if (glm::length(g.now - p) > 1.0f)
        {
            draw.line(g.now + Vec3(0.0f, 0.1f, 0.0f), p + Vec3(0.0f, 0.1f, 0.0f),
                      {Vec4(colour.r, colour.g, colour.b, 0.35f)});
        }
    }
    if (!figureShown)
    {
        (void)m_engine.setGhostFigure(std::nullopt);
    }
}

Result<RoutineEntryInfo> Editor::panelEntry() const
{
    const ui::RoutinePanel& p = m_routinePanel;
    if (!parseMinute(p.from) || !parseMinute(p.to))
    {
        return Error{"from and to are HH:MM"};
    }
    if (p.state < 0 || static_cast<usize>(p.state) >= p.states.size())
    {
        return Error{"choose a state"};
    }
    if (p.at.empty())
    {
        return Error{"choose a place (way point or freepoint)"};
    }
    return RoutineEntryInfo{p.from, p.to, p.states[static_cast<usize>(p.state)], p.at};
}

void Editor::fillRoutinePanel()
{
    ui::RoutinePanel& p = m_routinePanel;
    p.shown = true;
    p.playing = playing();
    if (p.playing)
    {
        return;
    }
    const std::vector<RoutineInfo> all = allRoutines();
    const u32 minute = static_cast<u32>(std::clamp(p.hour, 0.0f, 24.0f) * 60.0f) % 1440u;
    p.npcs.clear();
    for (const Ghost& g : ghosts(minute))
    {
        p.npcs.push_back(g.routine.empty() ? std::format("{}: no routine", g.npc)
                         : g.state.empty() ? std::format("{}: {} - nothing at this hour", g.npc, g.routine)
                                           : std::format("{}: {} - {} @ {}{}", g.npc, g.routine, g.state,
                                                         g.at, g.place ? "" : " (unknown place)"));
    }
    const std::string file = editorRoutineFile();
    p.routines.clear();
    p.selectedRoutine = -1;
    for (usize i = 0; i < all.size(); ++i)
    {
        p.routines.push_back(all[i].file == file ? all[i].name + "  (editor)" : all[i].name);
        if (all[i].name == m_selectedRoutine)
        {
            p.selectedRoutine = static_cast<i32>(i);
        }
    }
    p.entries.clear();
    if (p.selectedRoutine >= 0)
    {
        const RoutineInfo& r = all[static_cast<usize>(p.selectedRoutine)];
        p.file = "scripts/" + r.file;
        p.editable = r.file == file;
        for (const RoutineEntryInfo& e : r.entries)
        {
            p.entries.push_back(std::format("{}-{}  {} @ {}", e.from, e.to, e.state, e.at));
        }
        if (p.selectedEntry >= static_cast<i32>(p.entries.size()))
        {
            p.selectedEntry = -1;
        }
    }
    p.editorFile = "scripts/" + file;
    p.dirty = routinesDirty();
    p.states = m_engine.scriptGlobalNames("zs_");
    p.issues.clear();
    for (const RoutineIssue& issue : checkRoutines())
    {
        p.issues.push_back(std::format("{}: {}", issue.routine, issue.text));
    }
}

void Editor::applyRoutinePanel()
{
    ui::RoutinePanel& p = m_routinePanel;
    if (p.stop)
    {
        (void)stopPlay();
        return;
    }
    if (p.play || p.playAtStart)
    {
        (void)startPlay(p.playAtStart);
        return;
    }
    const std::vector<RoutineInfo> all = allRoutines();
    if (p.clickedNpc >= 0)
    {
        const std::vector<NpcRoutineInfo> npcs = m_engine.npcRoutines();
        if (static_cast<usize>(p.clickedNpc) < npcs.size())
        {
            m_selectedRoutine = npcs[static_cast<usize>(p.clickedNpc)].routine;
            p.selectedEntry = -1;
        }
    }
    if (p.clickedRoutine >= 0 && static_cast<usize>(p.clickedRoutine) < all.size())
    {
        m_selectedRoutine = all[static_cast<usize>(p.clickedRoutine)].name;
        p.selectedEntry = -1;
    }
    const auto selected = std::ranges::find(all, m_selectedRoutine, &RoutineInfo::name);
    if (p.clickedEntry >= 0 && selected != all.end() &&
        static_cast<usize>(p.clickedEntry) < selected->entries.size())
    {
        p.selectedEntry = p.clickedEntry;
        const RoutineEntryInfo& e = selected->entries[static_cast<usize>(p.clickedEntry)];
        p.from = e.from;
        p.to = e.to;
        p.at = e.at;
        const auto s = std::ranges::find(p.states, e.state);
        p.state = s != p.states.end() ? static_cast<i32>(s - p.states.begin()) : 0;
    }
    if (p.newRoutine)
    {
        (void)newRoutine(p.newName);
    }
    if (p.addEntry || p.changeEntry)
    {
        if (auto entry = panelEntry(); !entry)
        {
            m_status = entry.error().message;
        }
        else if (p.addEntry)
        {
            (void)addRoutineEntry(m_selectedRoutine, entry.value());
        }
        else if (p.selectedEntry >= 0)
        {
            (void)setRoutineEntry(m_selectedRoutine, static_cast<usize>(p.selectedEntry), entry.value());
        }
    }
    if (p.removeEntry && p.selectedEntry >= 0)
    {
        (void)removeRoutineEntry(m_selectedRoutine, static_cast<usize>(p.selectedEntry));
        p.selectedEntry = -1;
    }
    if (p.write)
    {
        (void)writeRoutines();
    }
}
} // namespace g7::editor

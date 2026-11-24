// The editor's terrain mode (M16 part F2, ADR 0023, decision 6): raise, lower, smooth, flatten and paint
// splat layers with the left mouse held; each stroke is one undo step holding the rectangle it touched (a
// delta, not the whole terrain); the drawn terrain follows at once, the collision when the stroke ends.
// Terrain from a world's generator (Leonberg) stays locked - except for the grass brush (M17 G3, decision
// 9): it paints the grass overlay over the generated mask, which the generator never writes.

#include <g7/core/Log.hpp>
#include <g7/editor/Editor.hpp>
#include <g7/platform/Input.hpp>
#include <g7/render/DebugDraw.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/DebugUi.hpp>
#include <g7/world/Terrain.hpp>

#include <format>

namespace g7::editor
{
namespace
{
constexpr f32 kRayStep = 0.5f;     ///< m between height checks along the mouse ray
constexpr f32 kRayLength = 600.0f; ///< m
constexpr std::array<const char*, 7> kModeNames = {"raise", "lower", "smooth",     "flatten",
                                                   "paint", "grass", "grass reset"};
} // namespace

std::string Editor::terrainLocked() const
{
    return m_engine.terrainLock();
}

void Editor::setTerrainMode(bool on)
{
    if (on && m_engine.terrain() == nullptr)
    {
        m_status = terrainLocked();
        on = false;
    }
    else if (on && !terrainLocked().empty())
    {
        // Generated terrain: only the grass overlay (M17 G3).
        m_status = terrainLocked() + " - only the grass brush";
        if (!paintsGrass(m_brush.mode))
        {
            m_brush.mode = BrushMode::Grass;
        }
    }
    if (m_stroke)
    {
        endStroke();
    }
    m_terrainMode = on;
    if (on)
    {
        setWaynetMode(false);
        setZoneMode(false);
        select({});
    }
}

TerrainTarget Editor::terrainTarget()
{
    TerrainTarget target;
    if (m_grassUsed) // M17 G3: created on first use - not for every undo
    {
        if (const auto overlay = m_engine.grassOverlay())
        {
            target.grass.push_back({overlay->rgba, overlay->width, overlay->height});
        }
    }
    target.heights = m_engine.editableHeightfield();
    if (target.heights == nullptr)
    {
        return target; // locked or none: the splat maps are not loaded for nothing
    }
    for (u32 m = 0;; ++m)
    {
        const auto view = m_engine.terrainSplat(m);
        if (!view)
        {
            break;
        }
        target.maps.push_back({view->rgba, view->width, view->height});
    }
    return target;
}

Result<void> Editor::beginStroke()
{
    if (paintsGrass(m_brush.mode)) // M17 G3: the grass overlay, also on locked terrain
    {
        const auto overlay = m_engine.terrain() != nullptr ? m_engine.grassOverlay() : std::nullopt;
        if (!overlay)
        {
            return Error{"the world has no terrain"};
        }
        m_grassUsed = true;
        Stroke stroke;
        stroke.grass.assign(overlay->rgba.begin(), overlay->rgba.end());
        m_stroke = std::move(stroke);
        return {};
    }
    world::Heightfield* field = m_engine.editableHeightfield();
    if (field == nullptr)
    {
        return Error{terrainLocked()};
    }
    Stroke stroke;
    stroke.heights.assign(field->samples().begin(), field->samples().end());
    if (m_brush.mode == BrushMode::Paint)
    {
        for (const SplatPixels& map : terrainTarget().maps)
        {
            stroke.splat.emplace_back(map.rgba.begin(), map.rgba.end());
        }
        if (stroke.splat.empty())
        {
            return Error{"the terrain has no splat maps to paint"};
        }
    }
    m_stroke = std::move(stroke);
    return {};
}

void Editor::strokeAt(const Vec3& centre, f32 seconds)
{
    if (m_stroke && paintsGrass(m_brush.mode))
    {
        const world::Heightfield* terrain = m_engine.terrain();
        const auto overlay = terrain != nullptr ? m_engine.grassOverlay() : std::nullopt;
        if (!overlay)
        {
            return;
        }
        const SplatPixels pixels{overlay->rgba, overlay->width, overlay->height};
        const std::array<SplatPixels, 1> maps{pixels};
        for (const SplatPatch& a : splatArea(maps, terrain->ref(), centre, m_brush.radius))
        {
            auto& known = m_stroke->grassArea;
            if (!known)
            {
                known = a;
                continue;
            }
            const u32 x0 = std::min(known->x, a.x);
            const u32 y0 = std::min(known->y, a.y);
            const u32 x1 = std::max(known->x + known->width, a.x + a.width);
            const u32 y1 = std::max(known->y + known->height, a.y + a.height);
            *known = {0, x0, y0, x1 - x0, y1 - y0, {}};
        }
        if (const auto touched = paintGrass(pixels, terrain->ref(), m_brush, centre, seconds))
        {
            m_engine.grassOverlayChanged(*touched);
        }
        return;
    }
    world::Heightfield* field = m_engine.editableHeightfield();
    if (!m_stroke || field == nullptr)
    {
        return;
    }
    if (m_brush.mode == BrushMode::Paint)
    {
        TerrainTarget target = terrainTarget();
        const auto areas = splatArea(target.maps, field->ref(), centre, m_brush.radius);
        for (const SplatPatch& a : areas)
        {
            auto known = std::ranges::find(m_stroke->splatAreas, a.map, &SplatPatch::map);
            if (known == m_stroke->splatAreas.end())
            {
                m_stroke->splatAreas.push_back(a);
                continue;
            }
            const u32 x0 = std::min(known->x, a.x);
            const u32 y0 = std::min(known->y, a.y);
            const u32 x1 = std::max(known->x + known->width, a.x + a.width);
            const u32 y1 = std::max(known->y + known->height, a.y + a.height);
            *known = {a.map, x0, y0, x1 - x0, y1 - y0, {}};
        }
        paintSplat(target.maps, field->ref(), m_brush, centre, seconds);
        if (auto changed = m_engine.terrainSplatChanged(); !changed)
        {
            m_status = changed.error().message;
        }
        return;
    }
    m_stroke->area = unite(m_stroke->area, brushArea(*field, centre, m_brush.radius));
    applyHeightBrush(*field, m_brush, centre, seconds);
    if (auto changed = m_engine.terrainHeightsChanged(); !changed)
    {
        m_status = changed.error().message;
    }
}

void Editor::endStroke()
{
    if (!m_stroke)
    {
        return;
    }
    Stroke stroke = std::move(*m_stroke);
    m_stroke.reset();
    if (stroke.grassArea) // M17 G3: the grass overlay's rectangle before and after
    {
        const auto overlay = m_engine.grassOverlay();
        if (!overlay)
        {
            return;
        }
        Command command{
            std::format("terrain: {}", kModeNames[static_cast<usize>(m_brush.mode)]), {}, {}, false};
        command.grassBefore.push_back(captureSplat(stroke.grass, overlay->width, *stroke.grassArea));
        command.grassAfter.push_back(captureSplat(overlay->rgba, overlay->width, *stroke.grassArea));
        if (command.grassBefore == command.grassAfter)
        {
            return; // nothing changed
        }
        m_history.record(std::move(command));
        m_grassDirty = true;
        return;
    }
    if (!stroke.grass.empty())
    {
        return; // a grass stroke that touched nothing
    }
    world::Heightfield* field = m_engine.editableHeightfield();
    if (field == nullptr)
    {
        return;
    }
    Command command{std::format("terrain: {}", kModeNames[static_cast<usize>(m_brush.mode)]), {}, {}, false};
    if (stroke.area.width > 0)
    {
        command.heightsBefore = captureHeights(stroke.heights, field->ref().width, stroke.area);
        command.heightsAfter = captureHeights(*field, stroke.area);
        if (command.heightsBefore == command.heightsAfter)
        {
            command.heightsBefore.reset();
            command.heightsAfter.reset();
        }
    }
    const TerrainTarget target = terrainTarget();
    for (const SplatPatch& area : stroke.splatAreas)
    {
        if (area.map < stroke.splat.size() && area.map < target.maps.size())
        {
            command.splatBefore.push_back(
                captureSplat(stroke.splat[area.map], target.maps[area.map].width, area));
            command.splatAfter.push_back(
                captureSplat(target.maps[area.map].rgba, target.maps[area.map].width, area));
        }
    }
    if (!command.heightsBefore && command.splatBefore.empty())
    {
        return; // nothing changed
    }
    m_history.record(std::move(command));
    m_terrainDirty = true;
    m_engine.terrainStrokeFinished(); // the collision anew
}

std::optional<Vec3> Editor::terrainUnder(const Vec2& mouse) const
{
    const world::Heightfield* field = m_engine.terrain();
    if (field == nullptr)
    {
        return std::nullopt;
    }
    // March along the ray until it passes below the surface, then halve the step.
    const Ray ray = screenRay(view(), mouse);
    f32 previous = 0.0f;
    for (f32 t = kRayStep; t < kRayLength; t += kRayStep)
    {
        const Vec3 p = ray.origin + ray.direction * t;
        if (p.y <= field->heightAt(p.x, p.z))
        {
            f32 lo = previous;
            f32 hi = t;
            for (int i = 0; i < 12; ++i)
            {
                const f32 mid = 0.5f * (lo + hi);
                const Vec3 q = ray.origin + ray.direction * mid;
                (q.y <= field->heightAt(q.x, q.z) ? hi : lo) = mid;
            }
            const Vec3 hit = ray.origin + ray.direction * hi;
            return Vec3(hit.x, field->heightAt(hit.x, hit.z), hit.z);
        }
        previous = t;
    }
    return std::nullopt;
}

void Editor::terrainMouse(Engine& engine, const Vec2& mouse, bool pressed, bool held, f32 seconds)
{
    const platform::Input& input = engine.input();
    const bool ctrl = input.isDown(platform::Key::LeftCtrl) || input.isDown(platform::Key::RightCtrl);
    const auto at = terrainUnder(mouse);
    if (at)
    {
        render::DebugDraw& draw = m_engine.debugDraw();
        draw.circle(*at + Vec3(0.0f, 0.1f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), m_brush.radius,
                    {Vec4(1.0f, 0.85f, 0.2f, 1.0f), 0.0f, false});
    }
    if (pressed && ctrl && at && m_brush.mode == BrushMode::Flatten)
    {
        m_brush.flattenHeight = at->y; // the height to flatten to, picked
        return;
    }
    if (pressed && !ctrl && !m_stroke)
    {
        if (auto begun = beginStroke(); !begun)
        {
            m_status = begun.error().message;
            return;
        }
    }
    if (m_stroke)
    {
        if (held && at)
        {
            strokeAt(*at, seconds);
        }
        else if (!held)
        {
            endStroke();
        }
    }
}

void Editor::fillTerrainPanel()
{
    ui::TerrainPanel& p = m_terrainPanel;
    p.shown = m_engine.terrain() != nullptr;
    if (!p.shown)
    {
        return;
    }
    p.locked = terrainLocked();
    p.active = m_terrainMode;
    p.dirty = m_terrainDirty || m_grassDirty;
    p.layers.clear();
    const u32 layers = m_engine.terrainLayerCount();
    for (u32 i = 0; i < layers; ++i)
    {
        p.layers.push_back(m_engine.terrain()->ref().layers[i].name);
    }
    p.mode = static_cast<i32>(m_brush.mode);
    p.radius = m_brush.radius;
    p.strength = m_brush.strength;
    p.flattenHeight = m_brush.flattenHeight;
    p.layer = static_cast<i32>(m_brush.layer);
    p.grassDensity = m_brush.grassDensity;
    p.grassMask = !m_engine.terrain()->ref().grassMask.empty();
}

void Editor::applyTerrainPanel()
{
    const ui::TerrainPanel& p = m_terrainPanel;
    if (p.active != m_terrainMode)
    {
        setTerrainMode(p.active);
    }
    if (!m_stroke) // a stroke keeps its brush
    {
        m_brush.mode = static_cast<BrushMode>(std::clamp(p.mode, 0, 6));
        if (!terrainLocked().empty() && !paintsGrass(m_brush.mode))
        {
            m_brush.mode = BrushMode::Grass; // generated terrain: only the grass overlay
        }
        m_brush.grassDensity = std::clamp(p.grassDensity, 0.0f, 1.0f);
        m_brush.radius = std::max(p.radius, 0.1f);
        m_brush.strength = std::max(p.strength, 0.01f);
        m_brush.flattenHeight = p.flattenHeight;
        m_brush.layer = static_cast<u32>(std::max(p.layer, 0));
    }
}
} // namespace g7::editor

// Grass fields (M17 part G1, render.md "Gras"): the engine side of render::GrassRing - the ground from the
// loaded world (heightfield, splat weights on the CPU) and where no grass may grow (rooms, water, the
// waynet's paths, under houses and mobs), the ring around the camera, the uploads and the passes.

#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>

namespace g7
{
namespace
{
constexpr f32 kPathHalfWidth = 0.6f; ///< the waynet's paths are 1.2 m wide (plan G1)
/// Geometry above this (relative to the ground) takes the tuft's place: also flat surfaces laid on the
/// terrain (garden paths, plazas), whose top may lie a little below the sampled ground.
constexpr f32 kFootprintClear = -0.15f;
constexpr f32 kFootprintProbe = 2.0f; ///< a ray from this high above the ground down to kFootprintClear

f32 segmentDistanceXz(const Vec2& p, const Vec2& a, const Vec2& b) noexcept
{
    const Vec2 ab = b - a;
    const f32 length2 = glm::dot(ab, ab);
    const f32 t = length2 > 1e-8f ? std::clamp(glm::dot(p - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * t));
}

/// The loaded world as ground for the grass.
class WorldGrassGround final : public render::GrassGround
{
public:
    WorldGrassGround(const world::Heightfield& heights, std::span<const asset::ImageData> splat,
                     std::span<const world::Zone> zones, const world::WaterBodies& water,
                     std::span<const std::pair<Vec3, Vec3>> paths, std::span<const AABB> solids,
                     const physics::PhysicsWorld& physics)
        : m_heights(heights), m_splat(splat), m_zones(zones), m_water(water), m_paths(paths),
          m_solids(solids), m_physics(physics)
    {
        m_bounds = heights.bounds();
    }

    void beginTile(const Vec2& min, const Vec2& max) override
    {
        const auto near = [&](const Vec2& centre, f32 radius)
        {
            const Vec2 closest = glm::clamp(centre, min, max);
            return glm::length(closest - centre) <= radius;
        };
        m_rooms.clear();
        for (const world::Zone& z : m_zones)
        {
            if (z.box && z.type == "indoor" &&
                near(Vec2(z.box->center.x, z.box->center.z),
                     glm::length(Vec2(z.box->halfExtents.x, z.box->halfExtents.z)) + 0.1f))
            {
                m_rooms.push_back({z.box->center, z.box->halfExtents, glm::radians(z.box->yawDegrees)});
            }
        }
        m_waterNear.clear();
        for (const world::WaterBody& b : m_water.bodies())
        {
            if (near(Vec2(b.centre.x, b.centre.z), glm::length(Vec2(b.halfExtents.x, b.halfExtents.z))))
            {
                m_waterNear.push_back(&b);
            }
        }
        m_pathsNear.clear();
        for (const auto& [a, b] : m_paths)
        {
            const Vec2 lo = glm::min(Vec2(a.x, a.z), Vec2(b.x, b.z)) - Vec2(kPathHalfWidth);
            const Vec2 hi = glm::max(Vec2(a.x, a.z), Vec2(b.x, b.z)) + Vec2(kPathHalfWidth);
            if (lo.x <= max.x && hi.x >= min.x && lo.y <= max.y && hi.y >= min.y)
            {
                m_pathsNear.emplace_back(Vec2(a.x, a.z), Vec2(b.x, b.z));
            }
        }
        // What stands there (houses, walls, rocks, mobs): their bounds; a tuft inside one gets a ray.
        m_solidsNear.clear();
        for (const AABB& box : m_solids)
        {
            if (box.min.x <= max.x && box.max.x >= min.x && box.min.z <= max.y && box.max.z >= min.y)
            {
                m_solidsNear.push_back(&box);
            }
        }
    }

    bool sample(const Vec2& xz, render::GrassGroundSample& out) const override
    {
        if (xz.x < m_bounds.min.x || xz.x > m_bounds.max.x || xz.y < m_bounds.min.z ||
            xz.y > m_bounds.max.z || m_heights.isHole(xz.x, xz.y))
        {
            return false;
        }
        out.height = m_heights.heightAt(xz.x, xz.y);
        out.normal = m_heights.normalAt(xz.x, xz.y);
        weights(xz, out.weights);
        return true;
    }

    bool excluded(const Vec3& foot) const override
    {
        const Vec2 xz(foot.x, foot.z);
        for (const auto& [a, b] : m_pathsNear)
        {
            if (segmentDistanceXz(xz, a, b) < kPathHalfWidth)
            {
                return true;
            }
        }
        const Vec3 above = foot + Vec3(0.0f, 0.02f, 0.0f);
        for (const world::WaterBody* b : m_waterNear)
        {
            if (b->covers(foot.x, foot.z) && above.y < b->surface() && above.y > b->bottom())
            {
                return true;
            }
        }
        if (!m_rooms.empty() && render::indoorAmount(foot + Vec3(0.0f, 0.1f, 0.0f), m_rooms, 0.0f) > 0.5f)
        {
            return true;
        }
        const bool under = std::ranges::any_of(m_solidsNear,
                                               [&](const AABB* b)
                                               {
                                                   return foot.x >= b->min.x && foot.x <= b->max.x &&
                                                          foot.z >= b->min.z && foot.z <= b->max.z &&
                                                          b->max.y > foot.y + kFootprintClear &&
                                                          b->min.y < foot.y + kFootprintProbe;
                                               });
        if (!under || !m_physics.valid())
        {
            return false;
        }
        const auto hit = m_physics.raycast(foot + Vec3(0.0f, kFootprintProbe, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
                                           kFootprintProbe - kFootprintClear,
                                           physics::layerBit(physics::Layer::World) |
                                               physics::layerBit(physics::Layer::Mob));
        return hit && hit->userData != 0;
    }

private:
    /// Bilinear splat weights (pixel centres on the samples, world.md).
    void weights(const Vec2& xz, std::array<f32, render::kGrassMaxLayers>& out) const
    {
        out.fill(0.0f);
        const world::TerrainRef& ref = m_heights.ref();
        if (ref.width < 2 || ref.height < 2)
        {
            return;
        }
        const f32 u = (xz.x - ref.firstSample.x) / ref.cellSize / static_cast<f32>(ref.width - 1);
        const f32 v = (xz.y - ref.firstSample.y) / ref.cellSize / static_cast<f32>(ref.height - 1);
        for (usize m = 0; m < m_splat.size(); ++m)
        {
            const asset::ImageData& image = m_splat[m];
            if (image.width == 0 || image.height == 0)
            {
                continue;
            }
            const f32 px = std::clamp(u, 0.0f, 1.0f) * static_cast<f32>(image.width - 1);
            const f32 py = std::clamp(v, 0.0f, 1.0f) * static_cast<f32>(image.height - 1);
            const u32 x0 = static_cast<u32>(px);
            const u32 y0 = static_cast<u32>(py);
            const u32 x1 = std::min(x0 + 1, image.width - 1);
            const u32 y1 = std::min(y0 + 1, image.height - 1);
            const f32 fx = px - static_cast<f32>(x0);
            const f32 fy = py - static_cast<f32>(y0);
            const auto texel = [&](u32 x, u32 y, usize k)
            {
                return static_cast<f32>(image.rgba8[(static_cast<usize>(y) * image.width + x) * 4 + k]) /
                       255.0f;
            };
            for (usize k = 0; k < 4 && m * 4 + k < out.size(); ++k)
            {
                const f32 top = glm::mix(texel(x0, y0, k), texel(x1, y0, k), fx);
                const f32 bottom = glm::mix(texel(x0, y1, k), texel(x1, y1, k), fx);
                out[m * 4 + k] = glm::mix(top, bottom, fy);
            }
        }
    }

    const world::Heightfield& m_heights;
    std::span<const asset::ImageData> m_splat;
    std::span<const world::Zone> m_zones;
    const world::WaterBodies& m_water;
    std::span<const std::pair<Vec3, Vec3>> m_paths;
    std::span<const AABB> m_solids;
    const physics::PhysicsWorld& m_physics;
    AABB m_bounds;
    std::vector<render::IndoorVolume> m_rooms;
    std::vector<const world::WaterBody*> m_waterNear;
    std::vector<std::pair<Vec2, Vec2>> m_pathsNear;
    std::vector<const AABB*> m_solidsNear;
};
} // namespace

void Engine::applyGrassSetting()
{
    const std::string text = m_config.settings.get<std::string>("render.grass", "auto");
    const bool intel = m_device && toLower(m_device->info().vendor).find("intel") != std::string::npos;
    render::GrassQuality quality = intel ? render::GrassQuality::Low : render::GrassQuality::Medium;
    if (text != "auto")
    {
        const auto parsed = render::parseGrassQuality(text);
        if (!parsed)
        {
            G7_LOG_WARN("engine", "[render] grass = {}: auto, off, low, medium or high (auto instead)", text);
        }
        quality = parsed.value_or(quality);
    }
    if (quality != m_grassQuality)
    {
        G7_LOG_INFO("engine", "grass: {}{}", render::grassQualityName(quality),
                    text == "auto" ? " (auto)" : "");
    }
    m_grassQuality = quality;
    m_grassDirty = true;
}

void Engine::resetGrassWorld()
{
    // Per world: the grass of each terrain layer by its name, the waynet's paths.
    m_grassLayers.clear();
    m_grassPaths.clear();
    m_grassSplatTried = false;
    m_grassDirty = true;
    if (!m_hasTerrain)
    {
        return;
    }
    for (const world::TerrainLayerRef& layer : m_heightfield.ref().layers)
    {
        m_grassLayers.push_back(render::builtinGrassLayer(layer.name));
    }
    const auto& points = m_waynet.points();
    for (u32 i = 0; i < points.size(); ++i)
    {
        for (const u32 j : m_waynet.neighbours(i))
        {
            if (i < j)
            {
                m_grassPaths.emplace_back(points[i].position, points[j].position);
            }
        }
    }
}

render::GrassFrame Engine::grassFrame() const
{
    render::GrassFrame frame;
    frame.settings = render::grassSettings(m_grassQuality);
    frame.wind = windNow();
    frame.layers = m_grassLayers;
    return frame;
}

void Engine::updateGrass()
{
    G7_PROFILE_SCOPE("render::grass ring");
    m_grassBuilt.clear();
    const bool wanted =
        m_grass.ready() && m_hasTerrain && m_grassQuality != render::GrassQuality::Off &&
        std::ranges::any_of(m_grassLayers, [](const render::GrassLayer& l) { return l.density > 0.0f; });
    if (m_grassDirty || !wanted)
    {
        const render::GrassSettings settings =
            render::grassSettings(wanted ? m_grassQuality : render::GrassQuality::Off);
        m_grassRing.reset(settings.radius, settings.tuftsPerSquareMetre);
        m_grassSolids.clear(); // collected anew below
        m_grassDirty = false;
        if (!wanted)
        {
            return;
        }
        if (auto reserved = m_grass.reserve(*m_device, m_grassRing); !reserved)
        {
            G7_LOG_WARN("engine", "grass: {} (no grass)", reserved.error().message);
            m_grassQuality = render::GrassQuality::Off;
            return;
        }
    }
    if (m_physicsDirty)
    {
        return; // the houses are not in the physics yet: wait, else grass grows in them
    }
    if (m_splatImages.empty() && !m_grassSplatTried && !m_heightfield.ref().splatMaps.empty())
    {
        m_grassSplatTried = true;
        (void)terrainSplat(0); // the weights on the CPU (loaded for the footsteps already, usually)
    }
    if (m_grassSolids.empty())
    {
        for (const SceneInstance& instance : m_instances)
        {
            if (instance.solid && instance.model != m_groundModel.get())
            {
                m_grassSolids.push_back(instance.bounds);
            }
        }
    }
    WorldGrassGround ground(m_heightfield, m_splatImages, m_worldFile.zones, m_water, m_grassPaths,
                            m_grassSolids, m_physics);
    m_grassRing.update(m_camera.transform.position, ground, m_grassLayers, m_grassBuilt);
    for (const u32 slot : m_grassBuilt)
    {
        if (auto uploaded = m_grass.upload(m_grassRing, slot); !uploaded)
        {
            G7_LOG_WARN("engine", "grass: {}", uploaded.error().message);
        }
    }
}

void Engine::drawGrass()
{
    m_grassDrawn = 0;
    if (m_grassRing.tiles().empty())
    {
        return;
    }
    m_meshRenderer.bindLighting(*m_device);
    m_grassDrawn = m_grass.draw(*m_device, m_camera, m_grassRing, grassFrame(), &m_frameLights);
}

void Engine::drawGrassShadow(const render::Cascade& cascade)
{
    if (!m_grassRing.tiles().empty())
    {
        m_grass.drawShadow(*m_device, cascade, m_camera.transform.position, m_grassRing, grassFrame());
    }
}
} // namespace g7

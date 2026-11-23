// Grass fields (M17 part G1, render.md "Gras"): the engine side of render::GrassRing - the ground from the
// loaded world (heightfield, splat weights on the CPU) and where no grass may grow (rooms, water, the
// waynet's paths, under houses and mobs), the ring around the camera, the uploads and the passes.

#include <g7/asset/ImageData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/runtime/AssetMounts.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/TerrainRef.hpp>

#include <algorithm>
#include <cmath>
#include <format>

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
                     const asset::ImageData* mask, const asset::ImageData* overlay,
                     std::span<const world::Zone> zones, const world::WaterBodies& water,
                     std::span<const std::pair<Vec3, Vec3>> paths, std::span<const AABB> solids,
                     const physics::PhysicsWorld& physics)
        : m_heights(heights), m_splat(splat), m_mask(mask), m_overlay(overlay), m_zones(zones),
          m_water(water), m_paths(paths), m_solids(solids), m_physics(physics)
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
        weights(xz, out);
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
    /// Bilinear splat weights and the grass mask (pixel centres on the samples, world.md).
    void weights(const Vec2& xz, render::GrassGroundSample& out) const
    {
        out.weights.fill(0.0f);
        const world::TerrainRef& ref = m_heights.ref();
        if (ref.width < 2 || ref.height < 2)
        {
            return;
        }
        const Vec2 uv((xz.x - ref.firstSample.x) / ref.cellSize / static_cast<f32>(ref.width - 1),
                      (xz.y - ref.firstSample.y) / ref.cellSize / static_cast<f32>(ref.height - 1));
        for (usize m = 0; m < m_splat.size(); ++m)
        {
            for (usize k = 0; k < 4 && m * 4 + k < out.weights.size(); ++k)
            {
                out.weights[m * 4 + k] = bilinear(m_splat[m], uv, k);
            }
        }
        if (m_mask != nullptr)
        {
            out.mask = bilinear(*m_mask, uv, 0); // M17 G2: terrain.grass_mask (grey: any channel)
        }
        if (m_overlay != nullptr) // M17 G3: the editor's overlay, premultiplied so its edge blends
        {
            const auto premultiplied = [](const u8* texel)
            { return static_cast<f32>(texel[0]) * static_cast<f32>(texel[3]) / (255.0f * 255.0f); };
            const auto coverage = [](const u8* texel) { return static_cast<f32>(texel[3]) / 255.0f; };
            out.mask = render::grassMaskWithOverlay(out.mask, bilinearOf(*m_overlay, uv, premultiplied),
                                                    bilinearOf(*m_overlay, uv, coverage));
        }
    }

    static f32 bilinear(const asset::ImageData& image, const Vec2& uv, usize channel)
    {
        return bilinearOf(image, uv,
                          [channel](const u8* texel) { return static_cast<f32>(texel[channel]) / 255.0f; });
    }
    /// `value(texel)` of the four pixels around uv, interpolated.
    template <typename Value>
    static f32 bilinearOf(const asset::ImageData& image, const Vec2& uv, Value value)
    {
        if (image.width == 0 || image.height == 0)
        {
            return 0.0f;
        }
        const f32 px = std::clamp(uv.x, 0.0f, 1.0f) * static_cast<f32>(image.width - 1);
        const f32 py = std::clamp(uv.y, 0.0f, 1.0f) * static_cast<f32>(image.height - 1);
        const u32 x0 = static_cast<u32>(px);
        const u32 y0 = static_cast<u32>(py);
        const u32 x1 = std::min(x0 + 1, image.width - 1);
        const u32 y1 = std::min(y0 + 1, image.height - 1);
        const auto texel = [&](u32 x, u32 y)
        { return value(image.rgba8.data() + (static_cast<usize>(y) * image.width + x) * 4); };
        const f32 fx = px - static_cast<f32>(x0);
        const f32 fy = py - static_cast<f32>(y0);
        return glm::mix(glm::mix(texel(x0, y0), texel(x1, y0), fx),
                        glm::mix(texel(x0, y1), texel(x1, y1), fx), fy);
    }
    const world::Heightfield& m_heights;
    std::span<const asset::ImageData> m_splat;
    const asset::ImageData* m_mask = nullptr;
    const asset::ImageData* m_overlay = nullptr;
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
    m_grassOverlay = {}; // M17 G3: looked for when the grass first grows (the world path is set by then)
    m_grassOverlayTried = false;
    if (!m_hasTerrain)
    {
        return;
    }
    // M17 G2: grass per layer name from data/grass.toml (the contract's values if it is missing or wrong).
    render::GrassTable table = render::GrassTable::builtin();
    const std::string tablePath = "data/grass.toml";
    if (auto bytes = m_vfs.read(tablePath); bytes)
    {
        auto parsed = render::GrassTable::parse(
            std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()),
            tablePath);
        if (parsed)
        {
            table = std::move(parsed).value();
        }
        else
        {
            G7_LOG_WARN("engine", "grass: {} (built-in values instead)", parsed.error().message);
        }
    }
    for (const world::TerrainLayerRef& layer : m_heightfield.ref().layers)
    {
        m_grassLayers.push_back(table.layer(layer.name));
    }
    // The optional mask (terrain.grass_mask): a density factor per splat pixel.
    m_grassMask = {};
    if (const std::string& mask = m_heightfield.ref().grassMask; !mask.empty())
    {
        const std::string path = preferCooked(m_vfs, mask);
        auto bytes = m_vfs.read(path);
        auto image = !bytes                    ? Result<asset::ImageData>(bytes.error())
                     : path.ends_with(".ktx2") ? asset::decodeKtx2Rgba(bytes.value(), path)
                                               : asset::decodeImage(bytes.value(), path);
        if (image)
        {
            m_grassMask = std::move(image).value();
        }
        else
        {
            G7_LOG_WARN("engine", "grass mask {}: {} (the splat weights alone)", path, image.error().message);
        }
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
    if (m_physicsDirty && m_paused)
    {
        // The editor pauses the simulation, which would sync the collision (moved vobs, undo): here then.
        syncPhysics();
    }
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
    if (!m_grassOverlayTried)
    {
        loadGrassOverlay();
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
    WorldGrassGround ground(m_heightfield, m_splatImages, m_grassMask.rgba8.empty() ? nullptr : &m_grassMask,
                            m_grassOverlay.rgba8.empty() ? nullptr : &m_grassOverlay, m_worldFile.zones,
                            m_water, m_grassPaths, m_grassSolids, m_physics);
    m_grassRing.update(m_camera.transform.position, ground, m_grassLayers, m_grassBuilt);
    m_grassBuildPeak = std::max(m_grassBuildPeak, m_grassRing.lastBuildMilliseconds());
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

// --- The editor's grass overlay (M17 G3, decision 9) ---

std::string Engine::grassOverlayPath() const
{
    return m_worldPath.empty() ? std::string() : world::grassOverlayPath(m_worldPath);
}

void Engine::loadGrassOverlay()
{
    m_grassOverlayTried = true;
    m_grassOverlay = {};
    const std::string source = grassOverlayPath();
    if (!m_hasTerrain || source.empty())
    {
        return;
    }
    const std::string path = preferCooked(m_vfs, source);
    if (!m_vfs.exists(path))
    {
        return; // none painted: the generated mask alone
    }
    auto bytes = m_vfs.read(path);
    auto image = !bytes                    ? Result<asset::ImageData>(bytes.error())
                 : path.ends_with(".ktx2") ? asset::decodeKtx2Rgba(bytes.value(), path)
                                           : asset::decodeImage(bytes.value(), path);
    if (!image)
    {
        G7_LOG_WARN("engine", "grass overlay {}: {} (the generated mask alone)", path, image.error().message);
        return;
    }
    m_grassOverlay = std::move(image).value();
    G7_LOG_INFO("engine", "grass overlay {} ({} x {})", path, m_grassOverlay.width, m_grassOverlay.height);
}

std::optional<Engine::SplatView> Engine::grassOverlay()
{
    if (!m_hasTerrain)
    {
        return std::nullopt;
    }
    if (!m_grassOverlayTried)
    {
        loadGrassOverlay();
    }
    if (m_grassOverlay.rgba8.empty())
    {
        // None yet: one pixel per sample, coverage 0 everywhere (the generated mask counts).
        const world::TerrainRef& ref = m_heightfield.ref();
        m_grassOverlay.width = ref.width;
        m_grassOverlay.height = ref.height;
        m_grassOverlay.rgba8.assign(static_cast<usize>(ref.width) * ref.height * 4, u8{0});
    }
    return SplatView{m_grassOverlay.rgba8, m_grassOverlay.width, m_grassOverlay.height};
}

void Engine::grassOverlayChanged(std::optional<std::pair<Vec2, Vec2>> area)
{
    if (area)
    {
        (void)m_grassRing.invalidate(area->first, area->second);
    }
    else
    {
        (void)m_grassRing.invalidateAll();
    }
}

Result<void> Engine::saveGrassOverlay()
{
    if (m_grassOverlay.rgba8.empty())
    {
        return {}; // nothing painted
    }
    const auto world = worldSourceFile();
    if (!world)
    {
        return Error{"the world is not a file on disk (archive) - its grass overlay cannot be written"};
    }
    const fs::Path target =
        world->parent_path() / fs::fromUtf8(world::grassOverlayPath(fs::toUtf8(world->filename())));
    if (auto saved = asset::savePng(target, m_grassOverlay); !saved)
    {
        return saved;
    }
    if (const std::string source = grassOverlayPath(); preferCooked(m_vfs, source) != source)
    {
        G7_LOG_WARN("engine", "{}: a cooked copy is there and wins until it is cooked anew", source);
    }
    G7_LOG_INFO("engine", "grass overlay saved: {}", fs::toUtf8(target));
    return {};
}

// --- Window "Grass" and the tile borders in the overlay (M17 G3) ---

void Engine::grassUi()
{
    ui::GrassPanel panel;
    const render::GrassStats& stats = m_grass.stats();
    panel.quality = static_cast<i32>(m_grassQuality);
    panel.radius = m_grassRing.radius();
    panel.tuftsPerSquareMetre = render::grassSettings(m_grassQuality).tuftsPerSquareMetre;
    panel.tiles = static_cast<u32>(m_grassRing.tiles().size());
    panel.slots = m_grassRing.slotCount();
    panel.stale = m_grassRing.staleCount();
    panel.pending = m_grassRing.pending();
    panel.tufts = m_grassRing.tuftCount();
    panel.drawnTiles = stats.tiles;
    panel.nearTufts = stats.nearTufts;
    panel.farTufts = stats.farTufts;
    panel.triangles = stats.triangles;
    panel.buildMilliseconds = m_grassRing.lastBuildMilliseconds();
    panel.buildPeakMilliseconds = m_grassBuildPeak;
    for (const render::PassTime& pass : m_passTimes.times())
    {
        if (pass.name == "grass")
        {
            panel.gpuMilliseconds = pass.milliseconds;
        }
    }
    panel.mask = !m_grassMask.rgba8.empty();
    panel.overlay = !m_grassOverlay.rgba8.empty();
    panel.borders = m_grassTileBorders;
    const i32 shownQuality = panel.quality;
    m_debugUi.grassPanel(panel);
    m_grassTileBorders = panel.borders;
    if (panel.resetPeak)
    {
        m_grassBuildPeak = 0.0;
    }
    if (panel.quality != shownQuality) // for this run only, the settings stay
    {
        m_grassQuality = static_cast<render::GrassQuality>(std::clamp(panel.quality, 0, 3));
        m_grassDirty = true;
    }
    else if (panel.rebuild)
    {
        m_grassDirty = true;
    }
}

void Engine::drawGrassTiles()
{
    if (!m_grassTileBorders || !m_hasTerrain || m_grassRing.tiles().empty())
    {
        return;
    }
    constexpr f32 kStep = 2.0f; // m between points of a border, following the ground
    constexpr f32 kLift = 0.15f;
    const auto ground = [&](f32 x, f32 z) { return Vec3(x, m_heightfield.heightAt(x, z) + kLift, z); };
    for (const render::GrassRing::Tile& tile : m_grassRing.tiles())
    {
        const Vec4 colour = tile.stale       ? Vec4(1.0f, 0.55f, 0.1f, 0.9f)
                            : tile.count > 0 ? Vec4(0.35f, 1.0f, 0.35f, 0.8f)
                                             : Vec4(0.6f, 0.6f, 0.6f, 0.5f);
        const render::DebugStyle style{colour};
        const Vec2 low(static_cast<f32>(tile.key.x) * render::kGrassTileSize,
                       static_cast<f32>(tile.key.z) * render::kGrassTileSize);
        const Vec2 corners[] = {low, low + Vec2(render::kGrassTileSize, 0.0f),
                                low + Vec2(render::kGrassTileSize), low + Vec2(0.0f, render::kGrassTileSize)};
        for (usize side = 0; side < 4; ++side)
        {
            const Vec2 a = corners[side];
            const Vec2 b = corners[(side + 1) % 4];
            for (f32 t = 0.0f; t < render::kGrassTileSize; t += kStep)
            {
                const Vec2 p = glm::mix(a, b, t / render::kGrassTileSize);
                const Vec2 q = glm::mix(a, b, (t + kStep) / render::kGrassTileSize);
                m_debugDraw.line(ground(p.x, p.y), ground(q.x, q.y), style);
            }
        }
        const Vec2 centre = low + Vec2(0.5f * render::kGrassTileSize);
        m_debugDraw.text(ground(centre.x, centre.y) + Vec3(0.0f, 0.5f, 0.0f),
                         std::format("{} {}{}", tile.key.x, tile.key.z,
                                     tile.count > 0 ? std::format(": {}", tile.count) : ""),
                         style);
    }
}
} // namespace g7

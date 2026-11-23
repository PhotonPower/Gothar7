#include <g7/core/Assert.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Grass.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>

namespace g7::render
{
namespace
{
constexpr u32 kTuftBinding = 6;   // std430 buffer of the tufts in grass.vert
constexpr u32 kWindBinding = 4;   // the Wind block of common/wind.glsl
constexpr f32 kWeightLow = 0.5f;  ///< a layer's share of the ground below this: none of its grass
constexpr f32 kWeightHigh = 0.8f; ///< above this: all of it
constexpr f32 kWindReach = 0.35f; // metres the wind can move a tip (culling bounds grow by it)

/// lowbias32 (Chris Wellons): a well mixed 32-bit hash.
constexpr u32 mix32(u32 x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/// A random generator for one candidate: the same tile and index always give the same numbers.
class TileRandom
{
public:
    TileRandom(GrassTileKey key, u32 index) noexcept
        : m_state(mix32(static_cast<u32>(key.x) * 0x9e3779b9U ^ mix32(static_cast<u32>(key.z) + 0x632be5abU) ^
                        mix32(index + 0x85ebca6bU)))
    {
    }
    /// 0 <= value < 1.
    f32 next() noexcept
    {
        m_state = mix32(m_state + 0x9e3779b9U);
        return static_cast<f32>(m_state >> 8) * (1.0f / 16777216.0f);
    }

private:
    u32 m_state;
};

u32 quantise(f32 value01, u32 steps) noexcept
{
    return std::min(static_cast<u32>(std::lround(std::clamp(value01, 0.0f, 1.0f) * static_cast<f32>(steps))),
                    steps);
}

/// Distance in xz from `p` to the tile's square (0 inside), and to its farthest corner.
f32 tileNearest(GrassTileKey key, const Vec2& p) noexcept
{
    const Vec2 min(static_cast<f32>(key.x) * kGrassTileSize, static_cast<f32>(key.z) * kGrassTileSize);
    const Vec2 closest = glm::clamp(p, min, min + Vec2(kGrassTileSize));
    return glm::length(closest - p);
}

f32 boxNearest(const AABB& box, const Vec3& p) noexcept
{
    return glm::length(glm::clamp(p, box.min, box.max) - p);
}

f32 boxFarthest(const AABB& box, const Vec3& p) noexcept
{
    return glm::length(glm::max(glm::abs(p - box.min), glm::abs(p - box.max)));
}
} // namespace

std::optional<GrassQuality> parseGrassQuality(std::string_view text) noexcept
{
    for (const GrassQuality q :
         {GrassQuality::Off, GrassQuality::Low, GrassQuality::Medium, GrassQuality::High})
    {
        if (text == grassQualityName(q))
        {
            return q;
        }
    }
    return std::nullopt;
}

std::string_view grassQualityName(GrassQuality quality) noexcept
{
    switch (quality)
    {
    case GrassQuality::Off:
        return "off";
    case GrassQuality::Low:
        return "low";
    case GrassQuality::Medium:
        return "medium";
    case GrassQuality::High:
        return "high";
    }
    return "medium";
}

GrassSettings grassSettings(GrassQuality quality) noexcept
{
    switch (quality)
    {
    case GrassQuality::Off:
        return {};
    case GrassQuality::Low:
        return {20.0f, 1.5f, 8.0f, 0.0f};
    case GrassQuality::Medium:
        return {35.0f, 3.0f, 12.0f, 0.0f};
    case GrassQuality::High:
        return {50.0f, 5.0f, 20.0f, 15.0f};
    }
    return {};
}

GrassLayer builtinGrassLayer(std::string_view name) noexcept
{
    GrassLayer layer;
    if (name == "Wiese" || name == "grass")
    {
        // Decision 1: ankle to knee high and loose, the ground shows through.
        layer.density = 1.0f;
        layer.height = {0.25f, 0.6f};
        layer.flowers = true;
    }
    return layer;
}

u32 packGrassTuft(const GrassTuftInfo& info) noexcept
{
    constexpr f32 kTwoPi = 2.0f * std::numbers::pi_v<f32>;
    f32 yaw = std::fmod(info.yaw, kTwoPi);
    yaw = yaw < 0.0f ? yaw + kTwoPi : yaw;
    const u32 yawBits = static_cast<u32>(std::lround(yaw / kTwoPi * 64.0f)) & 63u;
    const u32 heightBits = quantise(info.height / kGrassMaxHeight, 63);
    const u32 nx = quantise(info.normalXz.x * 0.5f + 0.5f, 63);
    const u32 nz = quantise(info.normalXz.y * 0.5f + 0.5f, 63);
    return yawBits | heightBits << 6 | (info.layer & 7u) << 12 | (info.tint & 7u) << 15 |
           (static_cast<u32>(info.kind) & 3u) << 18 | nx << 20 | nz << 26;
}

GrassTuftInfo unpackGrassTuft(u32 packed) noexcept
{
    GrassTuftInfo info;
    info.yaw = static_cast<f32>(packed & 63u) * (2.0f * std::numbers::pi_v<f32> / 64.0f);
    info.height = static_cast<f32>(packed >> 6 & 63u) / 63.0f * kGrassMaxHeight;
    info.layer = packed >> 12 & 7u;
    info.tint = packed >> 15 & 7u;
    info.kind = static_cast<GrassKind>(packed >> 18 & 3u);
    info.normalXz =
        Vec2(static_cast<f32>(packed >> 20 & 63u), static_cast<f32>(packed >> 26 & 63u)) / 63.0f * 2.0f -
        1.0f;
    return info;
}

GrassTileKey grassTileAt(const Vec2& xz) noexcept
{
    return {static_cast<i32>(std::floor(xz.x / kGrassTileSize)),
            static_cast<i32>(std::floor(xz.y / kGrassTileSize))};
}

namespace
{
u32 candidatesPerSide(f32 tuftsPerSquareMetre) noexcept
{
    if (tuftsPerSquareMetre <= 0.0f)
    {
        return 0;
    }
    return std::max(1u, static_cast<u32>(std::ceil(kGrassTileSize * std::sqrt(tuftsPerSquareMetre))));
}
} // namespace

u32 grassTileCapacity(f32 tuftsPerSquareMetre) noexcept
{
    const u32 n = candidatesPerSide(tuftsPerSquareMetre);
    return n * n;
}

void buildGrassTile(const GrassGround& ground, std::span<const GrassLayer> layers, GrassTileKey key,
                    f32 tuftsPerSquareMetre, std::vector<GrassTuft>& out)
{
    out.clear();
    const u32 n = candidatesPerSide(tuftsPerSquareMetre);
    if (n == 0 || layers.empty())
    {
        return;
    }
    const f32 cell = kGrassTileSize / static_cast<f32>(n);
    const Vec2 origin(static_cast<f32>(key.x) * kGrassTileSize, static_cast<f32>(key.z) * kGrassTileSize);
    const f32 minNormalY = std::cos(glm::radians(kGrassMaxSlope));
    const usize layerCount = std::min<usize>(layers.size(), kGrassMaxLayers);
    GrassGroundSample sample;
    for (u32 j = 0; j < n; ++j)
    {
        for (u32 i = 0; i < n; ++i)
        {
            TileRandom random(key, j * n + i);
            const Vec2 xz =
                origin +
                (Vec2(static_cast<f32>(i), static_cast<f32>(j)) + Vec2(random.next(), random.next())) * cell;
            const f32 keep = random.next();
            sample = GrassGroundSample{};
            if (!ground.sample(xz, sample) || sample.normal.y < minNormalY)
            {
                continue;
            }
            // Weights as the terrain shader blends them (normalised), sharpened: grass where its layer is the
            // ground one sees, none on a path whose edge only blends a little meadow in.
            f32 total = 0.0f;
            for (usize l = 0; l < layerCount; ++l)
            {
                total += std::max(sample.weights[l], 0.0f);
            }
            f32 density = 0.0f;
            f32 best = 0.0f;
            u32 bestLayer = 0;
            for (usize l = 0; l < layerCount && total > 1e-4f; ++l)
            {
                const f32 w =
                    glm::smoothstep(kWeightLow, kWeightHigh, std::max(sample.weights[l], 0.0f) / total);
                const f32 d = w * layers[l].density;
                density += d;
                if (d > best)
                {
                    best = d;
                    bestLayer = static_cast<u32>(l);
                }
            }
            density = std::min(density * std::clamp(sample.mask, 0.0f, 1.0f), 1.0f);
            if (keep >= density)
            {
                continue;
            }
            const Vec3 foot(xz.x, sample.height, xz.y);
            if (ground.excluded(foot))
            {
                continue;
            }
            const GrassLayer& layer = layers[bestLayer];
            GrassTuftInfo info;
            info.yaw = random.next() * 2.0f * std::numbers::pi_v<f32>;
            info.height = glm::mix(layer.height.x, layer.height.y, random.next());
            info.layer = bestLayer;
            info.tint = std::min(static_cast<u32>(random.next() * 8.0f), 7u);
            const f32 flower = random.next();
            const f32 colour = random.next();
            if (layer.flowers && flower < kGrassFlowerShare)
            {
                info.kind = static_cast<GrassKind>(1 + std::min(static_cast<u32>(colour * 3.0f), 2u));
            }
            info.normalXz = Vec2(sample.normal.x, sample.normal.z);
            out.push_back({foot, packGrassTuft(info)});
        }
    }
}

// --- GrassRing ---

void GrassRing::reset(f32 radius, f32 tuftsPerSquareMetre)
{
    m_tiles.clear();
    m_radius = std::max(radius, 0.0f);
    m_density = std::max(tuftsPerSquareMetre, 0.0f);
    m_capacity = grassTileCapacity(m_density);
    const u32 side =
        m_radius > 0.0f
            ? static_cast<u32>(std::floor(2.0f * (m_radius + kGrassKeepMargin) / kGrassTileSize)) + 2
            : 0;
    m_slotCount = std::min(side * side, kGrassMaxTiles);
    m_slots.assign(m_slotCount, {});
    m_freeSlots.clear();
    for (u32 s = m_slotCount; s > 0; --s)
    {
        m_freeSlots.push_back(s - 1); // slot 0 first
    }
    m_pending = 0;
    m_buildMilliseconds = 0.0;
}

void GrassRing::update(const Vec3& eye, GrassGround& ground, std::span<const GrassLayer> layers,
                       std::vector<u32>& built, u32 maxBuilds)
{
    m_buildMilliseconds = 0.0;
    m_pending = 0;
    if (m_radius <= 0.0f || m_slotCount == 0)
    {
        return;
    }
    const Vec2 p(eye.x, eye.z);
    // Drop what is too far now.
    std::erase_if(m_tiles,
                  [&](const Tile& t)
                  {
                      if (tileNearest(t.key, p) <= m_radius + kGrassKeepMargin)
                      {
                          return false;
                      }
                      m_freeSlots.push_back(t.slot);
                      return true;
                  });
    // Missing tiles within the radius, nearest first.
    m_wanted.clear();
    const GrassTileKey low = grassTileAt(p - Vec2(m_radius));
    const GrassTileKey high = grassTileAt(p + Vec2(m_radius));
    for (i32 z = low.z; z <= high.z; ++z)
    {
        for (i32 x = low.x; x <= high.x; ++x)
        {
            const GrassTileKey key{x, z};
            const f32 d = tileNearest(key, p);
            if (d > m_radius || std::ranges::any_of(m_tiles, [&](const Tile& t) { return t.key == key; }))
            {
                continue;
            }
            m_wanted.emplace_back(d, key);
        }
    }
    std::ranges::sort(
        m_wanted,
        [](const auto& a, const auto& b)
        {
            return a.first < b.first ||
                   (a.first == b.first &&
                    (a.second.z < b.second.z || (a.second.z == b.second.z && a.second.x < b.second.x)));
        });
    u32 builds = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& [distance, key] : m_wanted)
    {
        if (builds >= maxBuilds || m_freeSlots.empty())
        {
            break;
        }
        G7_PROFILE_SCOPE("render::grass tile");
        Tile tile;
        tile.key = key;
        tile.slot = m_freeSlots.back();
        m_freeSlots.pop_back();
        const Vec2 min(static_cast<f32>(key.x) * kGrassTileSize, static_cast<f32>(key.z) * kGrassTileSize);
        ground.beginTile(min, min + Vec2(kGrassTileSize));
        std::vector<GrassTuft>& tufts = m_slots[tile.slot];
        buildGrassTile(ground, layers, key, m_density, tufts);
        G7_ASSERT(tufts.size() <= m_capacity, "grass: a tile holds more tufts than its slot");
        tile.count = static_cast<u32>(tufts.size());
        tile.bounds = {Vec3(min.x, eye.y, min.y),
                       Vec3(min.x + kGrassTileSize, eye.y, min.y + kGrassTileSize)};
        if (!tufts.empty())
        {
            f32 lowY = tufts.front().position.y;
            f32 highY = lowY;
            for (const GrassTuft& t : tufts)
            {
                lowY = std::min(lowY, t.position.y);
                highY = std::max(highY, t.position.y);
            }
            tile.bounds.min.y = lowY;
            tile.bounds.max.y = highY + kGrassMaxHeight;
        }
        m_tiles.push_back(tile);
        built.push_back(tile.slot);
        ++builds;
    }
    m_buildMilliseconds =
        std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_pending = static_cast<u32>(m_wanted.size()) - builds;
}

std::span<const GrassTuft> GrassRing::tufts(u32 slot) const noexcept
{
    return slot < m_slots.size() ? std::span<const GrassTuft>(m_slots[slot]) : std::span<const GrassTuft>();
}

u32 GrassRing::tuftCount() const noexcept
{
    u32 count = 0;
    for (const Tile& t : m_tiles)
    {
        count += t.count;
    }
    return count;
}

// --- GrassRenderer ---

Result<GrassRenderer> GrassRenderer::create(Device& device, ShaderLibrary& shaders,
                                            const ShadowSettings& shadows)
{
    GrassRenderer grass;
    auto program = shaders.load("grass", {"grass.vert", "grass.frag", {}});
    if (!program)
    {
        return program.error();
    }
    auto shadowProgram = shaders.load("grass_shadow", {"grass.vert", "grass.frag", {"SHADOW"}});
    if (!shadowProgram)
    {
        return shadowProgram.error();
    }
    grass.m_program = program.value();
    grass.m_shadowProgram = shadowProgram.value();
    rhi::PipelineDesc desc;
    desc.program = grass.m_program;
    desc.cull = rhi::CullMode::None; // blades are seen from both sides
    auto pipeline = device.createPipeline(desc);
    if (!pipeline)
    {
        return pipeline.error();
    }
    grass.m_pipeline = std::move(pipeline).value();
    desc.program = grass.m_shadowProgram;
    desc.depthCompare = rhi::CompareOp::LessEqual; // shadow depth is not reversed
    desc.depthBias = {shadows.depthBias, shadows.slopeBias};
    auto shadowPipeline = device.createPipeline(desc);
    if (!shadowPipeline)
    {
        return shadowPipeline.error();
    }
    grass.m_shadowPipeline = std::move(shadowPipeline).value();
    auto wind = device.createBuffer({sizeof(GpuWind), rhi::BufferUsage::Dynamic, {}});
    if (!wind)
    {
        return wind.error();
    }
    grass.m_wind = std::move(wind).value();
    return grass;
}

Result<bool> GrassRenderer::reserve(Device& device, const GrassRing& ring)
{
    const usize size =
        std::max<usize>(static_cast<usize>(ring.slotCount()) * ring.slotCapacity(), 1) * sizeof(GrassTuft);
    if (m_tufts.size() == size && m_slotCapacity == ring.slotCapacity())
    {
        return false;
    }
    auto buffer = device.createBuffer({size, rhi::BufferUsage::Dynamic, {}});
    if (!buffer)
    {
        return buffer.error();
    }
    m_tufts = std::move(buffer).value();
    m_slotCapacity = ring.slotCapacity();
    return true;
}

Result<void> GrassRenderer::upload(const GrassRing& ring, u32 slot)
{
    const std::span<const GrassTuft> tufts = ring.tufts(slot);
    if (tufts.empty())
    {
        return {};
    }
    if (m_slotCapacity != ring.slotCapacity() || tufts.size() > m_slotCapacity)
    {
        return Error{"grass: buffer not reserved for this ring"};
    }
    return m_tufts.update(static_cast<usize>(slot) * m_slotCapacity * sizeof(GrassTuft),
                          std::span<const u8>(reinterpret_cast<const u8*>(tufts.data()), tufts.size_bytes()));
}

void GrassRenderer::collect(Stage& stage, const GrassRing& ring, const Frustum& view, const Vec3& eye,
                            f32 nearest, f32 farthest) const
{
    stage.tiles = 0;
    stage.instances = 0;
    for (const GrassRing::Tile& tile : ring.tiles())
    {
        if (tile.count == 0 || stage.tiles >= kGrassMaxTiles)
        {
            continue;
        }
        AABB bounds = tile.bounds;
        bounds.min -= Vec3(kWindReach, 0.0f, kWindReach);
        bounds.max += Vec3(kWindReach, 0.0f, kWindReach);
        // Within reach of the stage: some tuft nearer than `farthest`, some farther than `nearest`.
        if (boxNearest(bounds, eye) > farthest || boxFarthest(bounds, eye) < nearest ||
            !view.intersects(bounds))
        {
            continue;
        }
        stage.start[stage.tiles] = static_cast<i32>(stage.instances);
        stage.base[stage.tiles] = static_cast<i32>(tile.slot * m_slotCapacity);
        ++stage.tiles;
        stage.instances += tile.count;
    }
}

void GrassRenderer::setCommon(rhi::ShaderProgram& program, const GrassFrame& frame, const Vec3& eye) const
{
    program.setUniform("uCameraPosition", eye);
    program.setUniform("uRadius", frame.settings.radius);
    program.setUniform("uBladeDistance", frame.settings.bladeDistance);
    std::array<f32, kGrassMaxLayers * 6> colours{};
    for (usize i = 0; i < std::min<usize>(frame.layers.size(), kGrassMaxLayers); ++i)
    {
        const GrassLayer& l = frame.layers[i];
        const f32 values[6] = {l.colourFoot.r, l.colourFoot.g, l.colourFoot.b,
                               l.colourTip.r,  l.colourTip.g,  l.colourTip.b};
        std::copy(std::begin(values), std::end(values), colours.begin() + static_cast<std::ptrdiff_t>(i * 6));
    }
    program.setUniform("uLayerColours", std::span<const f32>(colours));
}

void GrassRenderer::drawStage(Device& device, rhi::ShaderProgram& program, const Stage& stage, bool nearStage)
{
    if (stage.instances == 0)
    {
        return;
    }
    program.setUniform("uStage", nearStage ? 0 : 1);
    program.setUniform("uTileCount", static_cast<i32>(stage.tiles));
    program.setUniform("uTileStart", std::span<const i32>(stage.start.data(), stage.tiles));
    program.setUniform("uTileBase", std::span<const i32>(stage.base.data(), stage.tiles));
    const u32 vertices = nearStage ? kNearVertices : kFarVertices;
    device.drawInstanced(vertices, stage.instances);
    m_stats.triangles += stage.instances * vertices / 3;
}

u32 GrassRenderer::draw(Device& device, const Camera& camera, const GrassRing& ring, const GrassFrame& frame,
                        const LightList* lights)
{
    G7_PROFILE_SCOPE("render::grass");
    m_stats = {};
    if (m_program == nullptr || ring.tiles().empty() || frame.settings.radius <= 0.0f || m_slotCapacity == 0)
    {
        return 0;
    }
    const Vec3& eye = camera.transform.position;
    const Frustum view = camera.frustum();
    Stage nearStage;
    Stage farStage;
    collect(nearStage, ring, view, eye, 0.0f, frame.settings.bladeDistance + 1.0f);
    collect(farStage, ring, view, eye, frame.settings.bladeDistance - 1.0f, frame.settings.radius);
    if (nearStage.instances + farStage.instances == 0)
    {
        return 0;
    }
    // Point lights: the nearest reaching the grass around the camera.
    m_selectedLights.clear();
    std::array<i32, LightList::kMaxPerObject> indices{};
    if (lights != nullptr)
    {
        const AABB around{eye - Vec3(frame.settings.radius), eye + Vec3(frame.settings.radius)};
        lights->selectFor(around, m_selectedLights);
    }
    for (usize i = 0; i < m_selectedLights.size() && i < indices.size(); ++i)
    {
        indices[i] = static_cast<i32>(m_selectedLights[i]);
    }
    const GpuWind wind = packWind(frame.wind);
    (void)m_wind.update(0, std::span<const u8>(reinterpret_cast<const u8*>(&wind), sizeof(wind)));
    device.bindStorageBuffer(kTuftBinding, m_tufts);
    device.bindUniformBuffer(kWindBinding, m_wind);
    device.bindPipeline(m_pipeline);
    rhi::ShaderProgram& p = *m_program;
    p.setUniform("uViewProjection", camera.viewProjection());
    p.setUniform("uMaxDistance", frame.settings.radius);
    setCommon(p, frame, eye);
    p.setUniform("uLightIndices", std::span<const i32>(indices));
    p.setUniform("uLightCount", static_cast<i32>(std::min<usize>(m_selectedLights.size(), indices.size())));
    drawStage(device, p, nearStage, true);
    drawStage(device, p, farStage, false);
    m_stats.tiles = std::max(nearStage.tiles, farStage.tiles);
    m_stats.nearTufts = nearStage.instances;
    m_stats.farTufts = farStage.instances;
    return nearStage.instances + farStage.instances;
}

void GrassRenderer::drawShadow(Device& device, const Cascade& cascade, const Vec3& eye, const GrassRing& ring,
                               const GrassFrame& frame)
{
    if (m_shadowProgram == nullptr || frame.settings.shadowDistance <= 0.0f || ring.tiles().empty() ||
        m_slotCapacity == 0)
    {
        return;
    }
    G7_PROFILE_SCOPE("render::grass shadow");
    const Frustum volume = Frustum::fromViewProjection(cascade.viewProjection);
    Stage stage;
    collect(stage, ring, volume, eye, 0.0f, frame.settings.shadowDistance);
    if (stage.instances == 0)
    {
        return;
    }
    const GpuWind wind = packWind(frame.wind);
    (void)m_wind.update(0, std::span<const u8>(reinterpret_cast<const u8*>(&wind), sizeof(wind)));
    device.bindStorageBuffer(kTuftBinding, m_tufts);
    device.bindUniformBuffer(kWindBinding, m_wind);
    device.bindPipeline(m_shadowPipeline);
    rhi::ShaderProgram& p = *m_shadowProgram;
    p.setUniform("uViewProjection", cascade.viewProjection);
    p.setUniform("uMaxDistance", frame.settings.shadowDistance);
    setCommon(p, frame, eye);
    const u32 triangles = m_stats.triangles;
    drawStage(device, p, stage, true);
    m_stats.triangles = triangles; // statistics count the main pass
}
} // namespace g7::render

#pragma once

// Grass fields (M17 part G1, render.md "Gras"): tufts of blades on the meadows, instanced on the GPU. The
// world is cut into 16 x 16 m tiles; for each tile near the camera the CPU rolls the tufts once
// (deterministically from the tile's coordinates, from the ground's splat weights, height and slope) and
// uploads them (16 bytes each). Two stages: near tufts as 3D blades, far ones as two crossed cards, blended
// with a dither; on the last quarter of the radius the tufts shrink into the ground and thin out. The wind
// is the vegetation's (render::Wind, weight linear from the foot). No collision, no gameplay.

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/Shadows.hpp>
#include <g7/render/Wind.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::render
{
class Device;
class ShaderLibrary;
class LightList;
struct Camera;

/// `[render] grass`: off, low, medium (default) or high.
enum class GrassQuality : u8
{
    Off,
    Low,
    Medium,
    High,
};
[[nodiscard]] std::optional<GrassQuality> parseGrassQuality(std::string_view text) noexcept;
[[nodiscard]] std::string_view grassQualityName(GrassQuality quality) noexcept;

/// What a quality draws (plan, table "Dichte und Sichtweite").
struct GrassSettings
{
    f32 radius = 0.0f;              ///< metres around the camera; the last 25 % fade out
    f32 tuftsPerSquareMetre = 0.0f; ///< on a full meadow
    f32 bladeDistance = 0.0f;       ///< 3D blades up to here, cards beyond (blended over 2 m)
    f32 shadowDistance = 0.0f;      ///< tufts nearer than this cast sun shadows (first cascade); 0 = none
};
[[nodiscard]] GrassSettings grassSettings(GrassQuality quality) noexcept;

inline constexpr f32 kGrassTileSize = 16.0f;
inline constexpr u32 kGrassTilesPerFrame = 2;         ///< new tiles built per frame at most
inline constexpr u32 kGrassStaleRebuildsPerFrame = 4; ///< stale tiles (grass brush) rebuilt per frame at most
inline constexpr f32 kGrassMaxSlope = 35.0f;          ///< degrees; steeper ground has no grass
inline constexpr f32 kGrassKeepMargin = 8.0f;         ///< a tile stays until this far beyond the radius
inline constexpr f32 kGrassFlowerShare = 0.05f;       ///< of the tufts on layers with flowers (decision 3)
inline constexpr u32 kGrassMaxLayers = 8;             ///< the terrain's splat layers
inline constexpr u32 kGrassMaxTiles = 96;             ///< resident tiles (high: 81 at most)

/// How grass grows on one splat layer of the terrain (G1: built in for the meadow; G2: data/grass.toml).
struct GrassLayer
{
    f32 density = 0.0f;                     ///< 0..1 of the quality's tufts per m² at full weight
    Vec2 height{0.25f, 0.6f};               ///< metres, smallest and tallest tuft
    Vec3 colourFoot{0.035f, 0.06f, 0.012f}; ///< linear albedo at the foot
    Vec3 colourTip{0.16f, 0.22f, 0.055f};   ///< and at the tips
    bool flowers = false;                   ///< a few flowering tufts (white, yellow, blue)
};
/// Grass per terrain layer name (G2, data/grass.toml; the names are a contract with welt, coordination.md):
///
///     version = 1
///     [layer.Wiese]
///     aliases = ["grass"]        # further names of the same layer (the test camp)
///     density = 1.0              # 0..1
///     height = [0.25, 0.6]       # metres, smallest and tallest tuft
///     foot = [0.035, 0.06, 0.012] # linear albedo at the foot
///     tip = [0.16, 0.22, 0.055]   # and at the tips
///     flowers = true
///
/// Names it does not list have no grass.
class GrassTable
{
public:
    /// The contract's values without the file: Wiese (alias grass) 1, Waldboden 0.25, Matsch 0.1.
    [[nodiscard]] static GrassTable builtin();
    /// Errors name the source and the key.
    [[nodiscard]] static Result<GrassTable> parse(std::string_view toml, std::string_view source);
    /// The layer of this name (or alias); no grass for an unknown name.
    [[nodiscard]] GrassLayer layer(std::string_view name) const;
    [[nodiscard]] usize size() const noexcept { return m_layers.size(); }

private:
    std::vector<std::pair<std::string, GrassLayer>> m_layers; ///< names and aliases
};
/// GrassTable::builtin().layer(name).
[[nodiscard]] GrassLayer builtinGrassLayer(std::string_view name);

/// The ground under one point, from the world (the engine reads its heightfield and splat maps).
struct GrassGroundSample
{
    f32 height = 0.0f;
    Vec3 normal{0.0f, 1.0f, 0.0f};
    std::array<f32, kGrassMaxLayers> weights{}; ///< splat weight 0..1 of each terrain layer
    f32 mask = 1.0f;                            ///< density factor (G2: terrain.grass_mask)
};

/// The density factor with the editor's overlay (G3, world::grassOverlayPath): the generated mask where the
/// overlay does not count, its value where it does - mask x (1 - coverage) + premultiplied, with
/// `premultiplied` = value x coverage (interpolated as such, so the overlay's edge does not darken).
[[nodiscard]] constexpr f32 grassMaskWithOverlay(f32 mask, f32 premultiplied, f32 coverage) noexcept
{
    return mask * (1.0f - coverage) + premultiplied;
}

/// Where grass may grow: implemented by the engine over its world (heightfield, splat weights, rooms,
/// water, paths, buildings).
class GrassGround
{
public:
    virtual ~GrassGround() = default;
    /// Called before the samples of a tile (world xz square): a chance to collect what lies there.
    virtual void beginTile(const Vec2& min, const Vec2& max) { (void)min, (void)max; }
    /// The ground at xz; false where nothing grows (outside the terrain, a hole).
    [[nodiscard]] virtual bool sample(const Vec2& xz, GrassGroundSample& out) const = 0;
    /// True if no tuft may stand at `foot` (a room, water, a path, under a house or a mob).
    [[nodiscard]] virtual bool excluded(const Vec3& foot) const
    {
        (void)foot;
        return false;
    }
};

/// One tuft as the shader reads it (16 bytes): foot position and packed bits.
struct GrassTuft
{
    Vec3 position{0.0f};
    u32 packed = 0;
};
static_assert(sizeof(GrassTuft) == 16);

/// Kinds of tufts: grass, or a flowering one.
enum class GrassKind : u8
{
    Grass,
    FlowerWhite,
    FlowerYellow,
    FlowerBlue,
};

/// The unpacked fields of a tuft.
struct GrassTuftInfo
{
    f32 yaw = 0.0f;    ///< radians 0..2 pi (64 steps)
    f32 height = 0.0f; ///< metres 0..kGrassMaxHeight (64 steps)
    u32 layer = 0;     ///< terrain layer (its colours)
    u32 tint = 0;      ///< 0..7 colour variation
    GrassKind kind = GrassKind::Grass;
    Vec2 normalXz{0.0f}; ///< ground normal x and z (-1..1, 6 bits each); y follows
};
inline constexpr f32 kGrassMaxHeight = 1.2f;
[[nodiscard]] u32 packGrassTuft(const GrassTuftInfo& info) noexcept;
[[nodiscard]] GrassTuftInfo unpackGrassTuft(u32 packed) noexcept;

struct GrassTileKey
{
    i32 x = 0; ///< tile column: world x from x * kGrassTileSize
    i32 z = 0;
    friend bool operator==(const GrassTileKey&, const GrassTileKey&) = default;
};
[[nodiscard]] GrassTileKey grassTileAt(const Vec2& xz) noexcept;

/// The tufts of one tile into `out` (cleared). Deterministic: the same tile, ground and density give the
/// same list. Candidates on a jittered grid (`tuftsPerSquareMetre` on a full meadow), each kept with the
/// density of its ground (the layer's share of the normalised splat weights, sharpened by smoothstep(0.5,
/// 0.8), x layer density x mask); none on holes, slopes over kGrassMaxSlope or where the ground excludes
/// them.
void buildGrassTile(const GrassGround& ground, std::span<const GrassLayer> layers, GrassTileKey key,
                    f32 tuftsPerSquareMetre, std::vector<GrassTuft>& out);
/// Most tufts a tile can hold at this density (the candidates).
[[nodiscard]] u32 grassTileCapacity(f32 tuftsPerSquareMetre) noexcept;

/// The tiles around the camera: which are resident (each in a slot of the GPU buffer), which to build next.
class GrassRing
{
public:
    struct Tile
    {
        GrassTileKey key;
        u32 slot = 0;
        u32 count = 0;
        AABB bounds;        ///< of the tufts (tallest included); empty tiles: the flat square
        bool stale = false; ///< built from an older ground (invalidate): rebuilt in place, shown until then
    };

    /// Starts over for a radius and density (no tiles).
    void reset(f32 radius, f32 tuftsPerSquareMetre);
    /// Drops tiles beyond the radius plus kGrassKeepMargin, rebuilds up to kGrassStaleRebuildsPerFrame stale
    /// tiles in their slots (nearest first), then builds up to `maxBuilds` missing tiles within the radius,
    /// nearest first. The slots built are appended to `built` (their tufts in tufts()).
    void update(const Vec3& eye, GrassGround& ground, std::span<const GrassLayer> layers,
                std::vector<u32>& built, u32 maxBuilds = kGrassTilesPerFrame);
    /// The ground changed in this xz rectangle (the editor's grass brush, G3): the resident tiles touching it
    /// become stale - they keep their tufts until rebuilt. Returns how many.
    u32 invalidate(const Vec2& min, const Vec2& max);
    /// Every resident tile stale (an undo of the grass brush).
    u32 invalidateAll();
    [[nodiscard]] u32 staleCount() const noexcept;

    [[nodiscard]] const std::vector<Tile>& tiles() const noexcept { return m_tiles; }
    [[nodiscard]] std::span<const GrassTuft> tufts(u32 slot) const noexcept;
    /// Slots the GPU buffer needs (enough for every tile within radius + margin).
    [[nodiscard]] u32 slotCount() const noexcept { return m_slotCount; }
    [[nodiscard]] u32 slotCapacity() const noexcept { return m_capacity; }
    [[nodiscard]] f32 radius() const noexcept { return m_radius; }
    /// Tiles within the radius not built yet after the last update.
    [[nodiscard]] u32 pending() const noexcept { return m_pending; }
    [[nodiscard]] u32 tuftCount() const noexcept;
    [[nodiscard]] f64 lastBuildMilliseconds() const noexcept { return m_buildMilliseconds; }

private:
    void build(Tile& tile, const Vec3& eye, GrassGround& ground, std::span<const GrassLayer> layers);

    std::vector<Tile> m_tiles;
    std::vector<std::vector<GrassTuft>> m_slots; ///< tufts per slot
    std::vector<u32> m_freeSlots;
    f32 m_radius = 0.0f;
    f32 m_density = 0.0f;
    u32 m_slotCount = 0;
    u32 m_capacity = 0;
    u32 m_pending = 0;
    f64 m_buildMilliseconds = 0.0;
    std::vector<std::pair<f32, GrassTileKey>> m_wanted; // scratch
};

/// Grass makes way (decisions 4 and 5 of the grass plan): blades within kGrassBendRadius of a figure bend
/// away from it, tufts within kGrassItemRadius of an item lying in the world are cut to kGrassItemHeight of
/// their height (it stays visible). Both in grass.vert, for the figures and items nearest the camera.
inline constexpr u32 kGrassMaxBenders = 16;
inline constexpr u32 kGrassMaxShorteners = 32;
inline constexpr f32 kGrassBendRadius = 0.8f;
inline constexpr f32 kGrassBendTip =
    0.6f; ///< sideways at the tip, share of the blade height, right at the figure
inline constexpr f32 kGrassBendReach = 2.0f; ///< m above or below the figure's feet a tuft still bends
inline constexpr f32 kGrassItemRadius = 0.5f;
inline constexpr f32 kGrassItemHeight = 1.0f / 3.0f;

/// Height factor of a tuft at `distance` (xz) from the nearest lying item: kGrassItemHeight within
/// kGrassItemRadius, rising to 1 over the next 0.15 m (as grass.vert).
[[nodiscard]] f32 grassItemScale(f32 distance) noexcept;
/// How far the tip of a blade `height` tall at `foot` is pushed away from a figure standing at `feet`
/// (xz; 0 beyond kGrassBendRadius or kGrassBendReach; as grass.vert; at relative height w the push is
/// this times w squared).
[[nodiscard]] Vec2 grassBendTip(const Vec3& foot, f32 height, const Vec3& feet) noexcept;
/// The points nearest `eye` within `within` m (xz), at most `max`, as (x, y, z, radius) for the shader.
void nearestGrassPoints(std::span<const Vec3> points, const Vec3& eye, f32 within, u32 max, f32 radius,
                        std::vector<Vec4>& out);

/// Per frame: the light and the wind the grass gets.
struct GrassFrame
{
    GrassSettings settings;
    WindFrame wind;                     ///< strength 0 with `[render] wind` off
    std::span<const GrassLayer> layers; ///< colours per terrain layer
    std::span<const Vec4> benders;      ///< figures: feet and radius (at most kGrassMaxBenders)
    std::span<const Vec4> shorteners;   ///< lying items: position and radius (at most kGrassMaxShorteners)
};

/// Statistics of the last draw.
struct GrassStats
{
    u32 tiles = 0;     ///< tiles drawn (either stage)
    u32 nearTufts = 0; ///< instances of the 3D stage
    u32 farTufts = 0;  ///< instances of the card stage
    u32 triangles = 0;
};

class GrassRenderer
{
public:
    static constexpr u32 kBladesPerTuft = 7;
    static constexpr u32 kNearVertices = kBladesPerTuft * 15; ///< 5 triangles a blade
    static constexpr u32 kFarVertices = 12;                   ///< two crossed quads

    GrassRenderer() = default;
    [[nodiscard]] static Result<GrassRenderer> create(Device& device, ShaderLibrary& shaders,
                                                      const ShadowSettings& shadows);

    /// The buffer for `ring`'s slots (anew when its size changed; then every tile must be uploaded again:
    /// returns true).
    [[nodiscard]] Result<bool> reserve(Device& device, const GrassRing& ring);
    /// Uploads the tufts of one slot.
    [[nodiscard]] Result<void> upload(const GrassRing& ring, u32 slot);

    /// Main pass, after the opaque scene: the lighting block and shadow atlas must be bound
    /// (MeshRenderer::bindLighting). Returns the instances drawn.
    u32 draw(Device& device, const Camera& camera, const GrassRing& ring, const GrassFrame& frame,
             const LightList* lights);
    /// Sun shadows of the near tufts (quality high: frame.settings.shadowDistance > 0) into a cascade.
    void drawShadow(Device& device, const Cascade& cascade, const Vec3& eye, const GrassRing& ring,
                    const GrassFrame& frame);
    [[nodiscard]] const GrassStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] bool ready() const noexcept { return m_program != nullptr; }

private:
    struct Stage
    {
        std::array<i32, kGrassMaxTiles> start{}; ///< first instance of each listed tile
        std::array<i32, kGrassMaxTiles> base{};  ///< its first tuft in the buffer
        u32 tiles = 0;
        u32 instances = 0;
    };
    void collect(Stage& stage, const GrassRing& ring, const Frustum& view, const Vec3& eye, f32 nearest,
                 f32 farthest) const;
    void setCommon(rhi::ShaderProgram& program, const GrassFrame& frame, const Vec3& eye) const;
    void drawStage(Device& device, rhi::ShaderProgram& program, const Stage& stage, bool nearStage);

    rhi::ShaderProgram* m_program = nullptr; // owned by the ShaderLibrary
    rhi::ShaderProgram* m_shadowProgram = nullptr;
    rhi::Pipeline m_pipeline;
    rhi::Pipeline m_shadowPipeline;
    rhi::Buffer m_tufts; // slots x capacity tufts (std430, binding 6)
    rhi::Buffer m_wind;  // GpuWind (binding 4)
    u32 m_slotCapacity = 0;
    GrassStats m_stats;
    std::vector<u32> m_selectedLights;
};
} // namespace g7::render

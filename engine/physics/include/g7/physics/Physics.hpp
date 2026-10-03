#pragma once

// Module: g7::physics (M5) - collision, queries and (later) the character controller, built on Jolt
// Physics (ADR 0004). Jolt is private: the API speaks in engine types only (PImpl).
// Specification: docs/modules/physics.md

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace g7::physics
{
[[nodiscard]] std::string_view moduleName() noexcept;

/// What a body is; queries filter by these (physics.md).
enum class Layer : u8
{
    World, ///< terrain, buildings, static props
    Npc,
    Item,
    Mob,
    Trigger,
    Water,
    Count
};
using LayerMask = u32;
[[nodiscard]] constexpr LayerMask layerBit(Layer layer) noexcept
{
    return 1u << static_cast<u32>(layer);
}
inline constexpr LayerMask kAllLayers = (1u << static_cast<u32>(Layer::Count)) - 1;

/// A collision shape made of parts, in model space (asset::CollisionPart: COL_ nodes or the render mesh).
struct ShapePart
{
    enum class Kind : u8
    {
        Hull, ///< convex hull of `points` (COL_HULL_, and COL_BOX_ as its 8 corners)
        Mesh, ///< triangles `indices` over `points` (COL_ meshes, or the render mesh without COL_)
    };
    Kind kind = Kind::Mesh;
    std::vector<Vec3> points;
    std::vector<u32> indices;
};

/// Heights for the terrain body: width x height samples, row 0 at the smallest z (render/world
/// convention). `holes`: one byte per cell, 0 = hole (may be empty).
struct HeightfieldDesc
{
    u32 width = 0;
    u32 height = 0;
    f32 cellSize = 1.0f;
    Vec2 firstSample{0.0f};
    std::span<const f32> heights;
    std::span<const u8> holes;
};

struct ShapeId
{
    u32 value = ~0u;
    [[nodiscard]] bool valid() const noexcept { return value != ~0u; }
};

struct BodyId
{
    u32 value = ~0u;
    [[nodiscard]] bool valid() const noexcept { return value != ~0u; }
};

struct RayHit
{
    f32 distance = 0.0f;
    Vec3 position{0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    Layer layer = Layer::World;
    u64 userData = 0; ///< what the body was added with (the engine: the vob id)
};

struct PhysicsSettings
{
    u32 maxBodies = 65536;
    u32 threads = 0; ///< worker threads for the simulation; 0 = hardware threads - 1
};

/// Statistics (overlay, benchmark).
struct PhysicsStats
{
    u32 bodies = 0;
    u32 shapes = 0;
    u32 triangles = 0; ///< in mesh shapes (terrain not counted)
};

/// The physical world of the loaded game world. Main thread; Jolt runs its jobs on its own workers.
class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    [[nodiscard]] static Result<PhysicsWorld> create(const PhysicsSettings& settings = {});

    /// A shape from parts (one part: that shape; several: a static compound). Shared by every body
    /// added with it. Fails for parts without usable geometry.
    [[nodiscard]] Result<ShapeId> createShape(std::span<const ShapePart> parts);
    /// A static body: `shape` placed by position, rotation and (also non-uniform) scale.
    [[nodiscard]] Result<BodyId> addStatic(ShapeId shape, const Vec3& position, const Quat& rotation,
                                           const Vec3& scale, Layer layer, u64 userData);
    /// The terrain as a static height field (layer World). Holes: a sample is left out only if all its
    /// cells are holes, so collision holes are at most as large as the drawn ones.
    [[nodiscard]] Result<BodyId> addHeightfield(const HeightfieldDesc& heights, u64 userData = 0);
    void remove(BodyId body);
    /// Removes every body and shape (another world is loaded).
    void clear();
    /// After adding many static bodies: rebuilds the broad phase for fast queries.
    void optimize();

    /// Simulation step (fixed step of the engine).
    void step(f64 seconds);

    [[nodiscard]] std::optional<RayHit> raycast(const Vec3& origin, const Vec3& direction, f32 maxDistance,
                                                LayerMask layers = kAllLayers) const;
    /// A sphere moved along `direction`: the first thing it touches.
    [[nodiscard]] std::optional<RayHit> sphereCast(const Vec3& origin, f32 radius, const Vec3& direction,
                                                   f32 maxDistance, LayerMask layers = kAllLayers) const;
    /// User data of every body the sphere overlaps (each once, in no particular order).
    [[nodiscard]] std::vector<u64> overlapSphere(const Vec3& centre, f32 radius,
                                                 LayerMask layers = kAllLayers) const;

    [[nodiscard]] PhysicsStats stats() const noexcept;
    [[nodiscard]] bool valid() const noexcept { return m_impl != nullptr; }

private:
    friend class CharacterController;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::physics

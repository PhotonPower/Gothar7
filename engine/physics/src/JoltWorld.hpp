#pragma once

// Internal: the Jolt side of PhysicsWorld, shared by Physics.cpp and Character.cpp.

#include <g7/physics/Physics.hpp>

// Jolt first: Jolt.h sets up its configuration for every other Jolt header.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace g7::physics
{
namespace detail
{
// --- Jolt's process-wide registration (allocator, factory, types), shared by every PhysicsWorld ---
void acquireJolt();
void releaseJolt();

// --- Layers: object layers are our Layer values; two broad phase layers (static, moving) ---
namespace BroadPhase
{
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr u32 kCount = 2;
} // namespace BroadPhase

inline bool isStatic(JPH::ObjectLayer layer)
{
    const auto l = static_cast<Layer>(layer);
    return l == Layer::World || l == Layer::Trigger || l == Layer::Water;
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhase::kCount; }
    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return isStatic(layer) ? BroadPhase::kStatic : BroadPhase::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == BroadPhase::kStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
    {
        return !isStatic(layer) || broadPhase == BroadPhase::kMoving; // static never collides with static
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter
{
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return !(isStatic(a) && isStatic(b));
    }
};

/// Queries: only bodies whose layer is in the mask.
class MaskFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit MaskFilter(LayerMask mask) : m_mask(mask) {}
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return (m_mask & (1u << layer)) != 0;
    }

private:
    LayerMask m_mask;
};

inline JPH::Vec3 toJolt(const Vec3& v)
{
    return {v.x, v.y, v.z};
}
inline Vec3 fromJolt(JPH::Vec3Arg v)
{
    return {v.GetX(), v.GetY(), v.GetZ()};
}
} // namespace detail

using namespace detail;

struct PhysicsWorld::Impl
{
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairs objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    JPH::PhysicsSystem system;
    std::vector<JPH::RefConst<JPH::Shape>> shapes;
    std::vector<JPH::BodyID> bodies; // index = BodyId
    u32 triangles = 0;

    Impl() { acquireJolt(); }
    ~Impl()
    {
        clear();
        jobs.reset();
        temp.reset();
        releaseJolt();
    }

    void clear()
    {
        JPH::BodyInterface& bi = system.GetBodyInterface();
        for (const JPH::BodyID id : bodies)
        {
            if (!id.IsInvalid())
            {
                bi.RemoveBody(id);
                bi.DestroyBody(id);
            }
        }
        bodies.clear();
        shapes.clear();
        triangles = 0;
    }

    Result<BodyId> add(const JPH::BodyCreationSettings& settings)
    {
        JPH::BodyInterface& bi = system.GetBodyInterface();
        const JPH::BodyID id = bi.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if (id.IsInvalid())
        {
            return Error{"physics: too many bodies"};
        }
        bodies.push_back(id);
        return BodyId{static_cast<u32>(bodies.size() - 1)};
    }

    std::optional<RayHit> hitOf(const JPH::BodyID& body, const JPH::SubShapeID& subShape,
                                const JPH::RVec3& position, f32 distance) const
    {
        JPH::BodyLockRead lock(system.GetBodyLockInterface(), body);
        if (!lock.Succeeded())
        {
            return std::nullopt;
        }
        const JPH::Body& b = lock.GetBody();
        RayHit hit;
        hit.distance = distance;
        hit.position = fromJolt(JPH::Vec3(position));
        hit.normal = fromJolt(b.GetWorldSpaceSurfaceNormal(subShape, position));
        hit.layer = static_cast<Layer>(b.GetObjectLayer());
        hit.userData = b.GetUserData();
        return hit;
    }
};
} // namespace g7::physics

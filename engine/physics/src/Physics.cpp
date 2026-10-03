#include "JoltWorld.hpp"

#include <g7/core/Log.hpp>

#include <algorithm>
#include <thread>
#include <unordered_set>

namespace g7::physics
{
std::string_view moduleName() noexcept
{
    return "physics";
}

namespace detail
{
namespace
{
std::mutex g_joltMutex;
u32 g_joltUsers = 0;
} // namespace

void acquireJolt()
{
    std::lock_guard lock(g_joltMutex);
    if (g_joltUsers++ == 0)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}

void releaseJolt()
{
    std::lock_guard lock(g_joltMutex);
    if (--g_joltUsers == 0)
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}
} // namespace detail

PhysicsWorld::PhysicsWorld() = default;
PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

Result<PhysicsWorld> PhysicsWorld::create(const PhysicsSettings& settings)
{
    PhysicsWorld world;
    world.m_impl = std::make_unique<Impl>();
    Impl& impl = *world.m_impl;
    impl.temp = std::make_unique<JPH::TempAllocatorImpl>(16u * 1024u * 1024u);
    const u32 hardware = std::max(1u, std::thread::hardware_concurrency());
    const int threads =
        static_cast<int>(settings.threads != 0 ? settings.threads : std::max(1u, hardware - 1));
    impl.jobs =
        std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    impl.system.Init(settings.maxBodies, 0, settings.maxBodies, settings.maxBodies, impl.broadPhaseLayers,
                     impl.objectVsBroadPhase, impl.objectPairs);
    impl.system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
    return world;
}

Result<ShapeId> PhysicsWorld::createShape(std::span<const ShapePart> parts)
{
    std::vector<JPH::RefConst<JPH::Shape>> pieces;
    u32 triangles = 0;
    for (const ShapePart& part : parts)
    {
        JPH::Shape::ShapeResult result;
        if (part.kind == ShapePart::Kind::Hull)
        {
            JPH::Array<JPH::Vec3> points;
            for (const Vec3& p : part.points)
            {
                points.push_back(toJolt(p));
            }
            JPH::ConvexHullShapeSettings hull(points.data(), static_cast<int>(points.size()), 0.0f);
            result = hull.Create();
        }
        else
        {
            if (part.indices.size() < 3 || part.indices.size() % 3 != 0)
            {
                continue;
            }
            JPH::VertexList vertices;
            for (const Vec3& p : part.points)
            {
                vertices.push_back(JPH::Float3(p.x, p.y, p.z));
            }
            JPH::IndexedTriangleList triangleList;
            for (usize i = 0; i + 2 < part.indices.size(); i += 3)
            {
                const u32 a = part.indices[i];
                const u32 b = part.indices[i + 1];
                const u32 c = part.indices[i + 2];
                if (a == b || b == c || a == c || std::max({a, b, c}) >= part.points.size())
                {
                    continue; // degenerate or broken
                }
                triangleList.push_back(JPH::IndexedTriangle(a, b, c));
            }
            if (triangleList.empty())
            {
                continue;
            }
            triangles += static_cast<u32>(triangleList.size());
            JPH::MeshShapeSettings mesh(std::move(vertices), std::move(triangleList));
            result = mesh.Create();
        }
        if (result.HasError())
        {
            G7_LOG_WARN("physics", "collision part skipped: {}", result.GetError().c_str());
            continue;
        }
        pieces.push_back(result.Get());
    }
    if (pieces.empty())
    {
        return Error{"physics: no usable collision geometry"};
    }
    JPH::RefConst<JPH::Shape> shape = pieces.front();
    if (pieces.size() > 1)
    {
        JPH::StaticCompoundShapeSettings compound;
        for (const auto& piece : pieces)
        {
            compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), piece);
        }
        auto result = compound.Create();
        if (result.HasError())
        {
            return Error{std::string("physics: ") + result.GetError().c_str()};
        }
        shape = result.Get();
    }
    m_impl->shapes.push_back(shape);
    m_impl->triangles += triangles;
    return ShapeId{static_cast<u32>(m_impl->shapes.size() - 1)};
}

Result<BodyId> PhysicsWorld::addStatic(ShapeId shape, const Vec3& position, const Quat& rotation,
                                       const Vec3& scale, Layer layer, u64 userData)
{
    if (!shape.valid() || shape.value >= m_impl->shapes.size())
    {
        return Error{"physics: unknown shape"};
    }
    JPH::RefConst<JPH::Shape> placed = m_impl->shapes[shape.value];
    if (glm::any(glm::greaterThan(glm::abs(scale - Vec3(1.0f)), Vec3(1e-4f))))
    {
        placed = new JPH::ScaledShape(placed, toJolt(scale));
    }
    JPH::BodyCreationSettings settings(placed, JPH::RVec3(position.x, position.y, position.z),
                                       JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w).Normalized(),
                                       JPH::EMotionType::Static, static_cast<JPH::ObjectLayer>(layer));
    settings.mUserData = userData;
    return m_impl->add(settings);
}

Result<BodyId> PhysicsWorld::addHeightfield(const HeightfieldDesc& h, u64 userData)
{
    if (h.width < 2 || h.height < 2 || h.heights.size() != static_cast<usize>(h.width) * h.height ||
        !(h.cellSize > 0.0f))
    {
        return Error{"physics: invalid height field"};
    }
    const usize cells = static_cast<usize>(h.width - 1) * (h.height - 1);
    if (!h.holes.empty() && h.holes.size() != cells)
    {
        return Error{"physics: hole mask does not match the height field"};
    }
    // Jolt wants a square grid whose side is a multiple of its block size (2): pad with "no collision".
    u32 side = std::max(h.width, h.height);
    side += side % 2;
    std::vector<float> samples(static_cast<usize>(side) * side,
                               JPH::HeightFieldShapeConstants::cNoCollisionValue);
    const auto isHole = [&](i64 c, i64 r)
    {
        if (h.holes.empty())
        {
            return false;
        }
        if (c < 0 || r < 0 || c >= static_cast<i64>(h.width - 1) || r >= static_cast<i64>(h.height - 1))
        {
            return true; // outside the area counts as a hole for the corner rule below
        }
        return h.holes[static_cast<usize>(r) * (h.width - 1) + static_cast<usize>(c)] == 0;
    };
    for (u32 r = 0; r < h.height; ++r)
    {
        for (u32 c = 0; c < h.width; ++c)
        {
            // A sample takes the triangles of all four cells around it with it: leave it out only if all
            // of them are holes (the collision hole is never larger than the drawn one).
            const i64 ci = c;
            const i64 ri = r;
            const bool hole =
                isHole(ci - 1, ri - 1) && isHole(ci, ri - 1) && isHole(ci - 1, ri) && isHole(ci, ri);
            samples[static_cast<usize>(r) * side + c] =
                hole ? JPH::HeightFieldShapeConstants::cNoCollisionValue
                     : h.heights[static_cast<usize>(r) * h.width + c];
        }
    }
    JPH::HeightFieldShapeSettings settings(samples.data(), JPH::Vec3(h.firstSample.x, 0.0f, h.firstSample.y),
                                           JPH::Vec3(h.cellSize, 1.0f, h.cellSize), side);
    auto shape = settings.Create();
    if (shape.HasError())
    {
        return Error{std::string("physics: height field: ") + shape.GetError().c_str()};
    }
    JPH::BodyCreationSettings body(shape.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                   JPH::EMotionType::Static, static_cast<JPH::ObjectLayer>(Layer::World));
    body.mUserData = userData;
    return m_impl->add(body);
}

void PhysicsWorld::remove(BodyId body)
{
    if (!body.valid() || body.value >= m_impl->bodies.size() || m_impl->bodies[body.value].IsInvalid())
    {
        return;
    }
    JPH::BodyInterface& bi = m_impl->system.GetBodyInterface();
    bi.RemoveBody(m_impl->bodies[body.value]);
    bi.DestroyBody(m_impl->bodies[body.value]);
    m_impl->bodies[body.value] = JPH::BodyID();
}

void PhysicsWorld::clear()
{
    m_impl->clear();
}

void PhysicsWorld::optimize()
{
    m_impl->system.OptimizeBroadPhase();
}

void PhysicsWorld::step(f64 seconds)
{
    m_impl->system.Update(static_cast<float>(seconds), 1, m_impl->temp.get(), m_impl->jobs.get());
}

std::optional<RayHit> PhysicsWorld::raycast(const Vec3& origin, const Vec3& direction, f32 maxDistance,
                                            LayerMask layers) const
{
    const Vec3 dir = glm::normalize(direction);
    const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), toJolt(dir * maxDistance));
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    const MaskFilter filter(layers);
    m_impl->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), collector, {}, filter);
    if (!collector.HadHit())
    {
        return std::nullopt;
    }
    const f32 fraction = collector.mHit.mFraction;
    return m_impl->hitOf(collector.mHit.mBodyID, collector.mHit.mSubShapeID2, ray.GetPointOnRay(fraction),
                         fraction * maxDistance);
}

std::optional<RayHit> PhysicsWorld::sphereCast(const Vec3& origin, f32 radius, const Vec3& direction,
                                               f32 maxDistance, LayerMask layers) const
{
    const Vec3 dir = glm::normalize(direction);
    const JPH::SphereShape sphere(radius);
    const JPH::RShapeCast cast(&sphere, JPH::Vec3::sReplicate(1.0f),
                               JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z)),
                               toJolt(dir * maxDistance));
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const MaskFilter filter(layers);
    m_impl->system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {},
                                                   filter);
    if (!collector.HadHit())
    {
        return std::nullopt;
    }
    const auto& h = collector.mHit;
    JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), h.mBodyID2);
    if (!lock.Succeeded())
    {
        return std::nullopt;
    }
    RayHit hit;
    hit.distance = h.mFraction * maxDistance;
    hit.position = fromJolt(JPH::Vec3(h.mContactPointOn2));
    hit.normal = glm::normalize(fromJolt(-h.mPenetrationAxis));
    hit.layer = static_cast<Layer>(lock.GetBody().GetObjectLayer());
    hit.userData = lock.GetBody().GetUserData();
    return hit;
}

std::vector<u64> PhysicsWorld::overlapSphere(const Vec3& centre, f32 radius, LayerMask layers) const
{
    const JPH::SphereShape sphere(radius);
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const MaskFilter filter(layers);
    m_impl->system.GetNarrowPhaseQuery().CollideShape(
        &sphere, JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(centre.x, centre.y, centre.z)), JPH::CollideShapeSettings(),
        JPH::RVec3::sZero(), collector, {}, filter);
    std::vector<u64> result;
    std::unordered_set<u32> seen;
    for (const auto& hit : collector.mHits)
    {
        if (!seen.insert(hit.mBodyID2.GetIndexAndSequenceNumber()).second)
        {
            continue;
        }
        JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), hit.mBodyID2);
        if (lock.Succeeded())
        {
            result.push_back(lock.GetBody().GetUserData());
        }
    }
    return result;
}

PhysicsStats PhysicsWorld::stats() const noexcept
{
    if (!m_impl)
    {
        return {};
    }
    u32 bodies = 0;
    for (const JPH::BodyID id : m_impl->bodies)
    {
        bodies += id.IsInvalid() ? 0 : 1;
    }
    return {bodies, static_cast<u32>(m_impl->shapes.size()), m_impl->triangles};
}
} // namespace g7::physics

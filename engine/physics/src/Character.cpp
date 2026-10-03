#include "JoltWorld.hpp"

#include <g7/physics/Character.hpp>

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace g7::physics
{
namespace
{
/// The character is an upright cylinder, not a capsule. A capsule's rounded bottom slides up the edges
/// of steps (Jolt judges an edge by the step's flat top face), so the highest step it gets onto depends
/// on where in a fixed step the edge is met: measured with a 0.4 m step limit, 0.31 m blocked while 0.53 m
/// climbed. With a flat bottom only Jolt's stair walk lifts it: steps up to the limit (+ 1 cm) always,
/// above never, walking and running (tests/physics/test_character.cpp). The small convex radius keeps
/// that edge sharp (0.05 m: a 7 cm ledge failed at walking speed). On slopes the flat bottom stands on
/// its rim, so its centre hovers (r tan slope, 0.25 m at 40 degrees): visualFeet() gives the ground below
/// the centre for drawing.
constexpr float kConvexRadius = 0.01f;
} // namespace

struct CharacterController::Impl
{
    PhysicsWorld::Impl* world = nullptr;
    CharacterDesc desc;
    JPH::Ref<JPH::CharacterVirtual> character;
    MaskFilter filter{0};
    f32 pendingJump = 0.0f;     // upward speed for the next update
    bool falling = false;       // in the air since leaving the ground
    f32 fallTop = 0.0f;         // highest feet height of this fall
    std::optional<f32> landing; // fall height of the last landing, until taken

    explicit Impl(PhysicsWorld::Impl& w, const CharacterDesc& d) : world(&w), desc(d), filter(d.collidesWith)
    {
    }

    void refreshContacts() { character->RefreshContacts({}, filter, {}, {}, *world->temp); }

    /// True if the character's shape at `feet` (lifted by 2 cm) touches nothing it collides with.
    [[nodiscard]] bool roomAt(const JPH::RVec3& feet) const
    {
        const JPH::Shape* shape = character->GetShape();
        const JPH::RMat44 com =
            JPH::RMat44::sTranslation(feet + JPH::Vec3(0.0f, 0.02f, 0.0f) + shape->GetCenterOfMass());
        JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> hit;
        world->system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), com,
                                                         JPH::CollideShapeSettings(), JPH::RVec3::sZero(),
                                                         hit, {}, filter);
        return !hit.HadHit();
    }

    /// Closest hit of a ray against what the character collides with: the fraction, or nullopt.
    [[nodiscard]] std::optional<JPH::RayCastResult> ray(const JPH::RVec3& origin,
                                                        const JPH::Vec3& direction) const
    {
        JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> hit;
        world->system.GetNarrowPhaseQuery().CastRay(JPH::RRayCast(origin, direction), JPH::RayCastSettings(),
                                                    hit, {}, filter);
        return hit.HadHit() ? std::optional<JPH::RayCastResult>(hit.mHit) : std::nullopt;
    }
};

CharacterController::CharacterController() = default;
CharacterController::~CharacterController() = default;
CharacterController::CharacterController(CharacterController&&) noexcept = default;
CharacterController& CharacterController::operator=(CharacterController&&) noexcept = default;

Result<CharacterController> CharacterController::create(PhysicsWorld& world, const CharacterDesc& desc,
                                                        const Vec3& feet)
{
    if (!world.valid())
    {
        return Error{"physics: character without a physics world"};
    }
    if (!(desc.radius > 0.0f) || !(desc.height > 2.0f * desc.radius))
    {
        return Error{"physics: character height must exceed twice its radius"};
    }
    // Cylinder standing on the origin: the character's position is its feet.
    auto shape = JPH::RotatedTranslatedShapeSettings(
                     JPH::Vec3(0.0f, 0.5f * desc.height, 0.0f), JPH::Quat::sIdentity(),
                     new JPH::CylinderShape(0.5f * desc.height, desc.radius, kConvexRadius))
                     .Create();
    if (shape.HasError())
    {
        return Error{std::string("physics: character shape: ") + shape.GetError().c_str()};
    }
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = shape.Get();
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(desc.maxSlopeDegrees);
    // Only contacts in the lowest part count as ground (walls are no floor).
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius);

    CharacterController controller;
    controller.m_impl = std::make_unique<Impl>(*world.m_impl, desc);
    controller.m_impl->character =
        new JPH::CharacterVirtual(settings, JPH::RVec3(feet.x, feet.y, feet.z), JPH::Quat::sIdentity(),
                                  desc.userData, &world.m_impl->system);
    controller.m_impl->refreshContacts();
    return controller;
}

void CharacterController::update(f32 seconds, const Vec3& horizontalVelocity)
{
    JPH::CharacterVirtual& c = *m_impl->character;
    const JPH::Vec3 gravity = m_impl->world->system.GetGravity();
    const JPH::Vec3 current = c.GetLinearVelocity();
    JPH::Vec3 wanted(horizontalVelocity.x, 0.0f, horizontalVelocity.z);
    JPH::Vec3 velocity;
    const bool jumping = m_impl->pendingJump > 0.0f;
    switch (c.GetGroundState())
    {
    case JPH::CharacterBase::EGroundState::OnGround:
        // Walking: the wanted velocity on top of what the ground does; gravity keeps the contact.
        velocity = c.GetGroundVelocity() + wanted + gravity * seconds;
        if (jumping)
        {
            velocity.SetY(m_impl->pendingJump);
        }
        break;
    case JPH::CharacterBase::EGroundState::OnSteepGround:
    {
        // Too steep: no walking uphill, keep falling along the slope.
        JPH::Vec3 downhill = c.GetGroundNormal();
        downhill.SetY(0.0f);
        if (downhill.LengthSq() > 1e-8f)
        {
            downhill = downhill.Normalized();
            const float uphill = -wanted.Dot(downhill);
            if (uphill > 0.0f)
            {
                wanted += downhill * uphill;
            }
        }
        velocity = wanted + JPH::Vec3(0.0f, current.GetY(), 0.0f) + gravity * seconds;
        break;
    }
    default:
        // In the air: no steering, gravity.
        velocity = current + gravity * seconds;
        break;
    }
    c.SetLinearVelocity(velocity);
    m_impl->pendingJump = 0.0f;
    const f32 before = static_cast<f32>(c.GetPosition().GetY());

    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    // A jump must not be pulled back onto the ground or turned into a stair step.
    update.mWalkStairsStepUp = JPH::Vec3(0.0f, jumping ? 0.0f : m_impl->desc.stepHeight, 0.0f);
    update.mStickToFloorStepDown = JPH::Vec3(0.0f, jumping ? 0.0f : -m_impl->desc.stickToFloor, 0.0f);
    c.ExtendedUpdate(seconds, gravity, update, {}, m_impl->filter, {}, {}, *m_impl->world->temp);

    // Falls: from the highest point in the air down to the first ground contact.
    const f32 y = static_cast<f32>(c.GetPosition().GetY());
    if (!c.IsSupported())
    {
        if (!m_impl->falling)
        {
            m_impl->falling = true;
            m_impl->fallTop = before;
        }
        m_impl->fallTop = std::max(m_impl->fallTop, y);
    }
    else if (m_impl->falling)
    {
        m_impl->falling = false;
        m_impl->landing = std::max(0.0f, m_impl->fallTop - y);
    }
}

void CharacterController::swim(f32 seconds, const Vec3& velocity)
{
    JPH::CharacterVirtual& c = *m_impl->character;
    m_impl->pendingJump = 0.0f;
    m_impl->falling = false;
    c.SetLinearVelocity(JPH::Vec3(velocity.x, velocity.y, velocity.z));
    c.Update(seconds, JPH::Vec3::sZero(), {}, m_impl->filter, {}, {}, *m_impl->world->temp);
}

bool CharacterController::jump(f32 upwardSpeed)
{
    if (m_impl->character->GetGroundState() != JPH::CharacterBase::EGroundState::OnGround ||
        !(upwardSpeed > 0.0f))
    {
        return false;
    }
    m_impl->pendingJump = upwardSpeed;
    return true;
}

void CharacterController::moveTo(const Vec3& feet)
{
    m_impl->character->SetPosition(JPH::RVec3(feet.x, feet.y, feet.z));
    m_impl->character->SetLinearVelocity(JPH::Vec3::sZero());
    m_impl->falling = false;
}

std::optional<f32> CharacterController::takeLanding()
{
    return std::exchange(m_impl->landing, std::nullopt);
}

std::optional<Ledge> CharacterController::findLedge(const Vec3& direction, f32 minHeight, f32 maxHeight,
                                                    f32 reach) const
{
    const Impl& m = *m_impl;
    const JPH::Vec3 dir = JPH::Vec3(direction.x, 0.0f, direction.z).NormalizedOr(JPH::Vec3::sAxisX());
    const JPH::RVec3 feet = m.character->GetPosition();
    const f32 r = m.desc.radius;
    const f32 cosMaxSlope = std::cos(glm::radians(m.desc.maxSlopeDegrees));
    // 1. The top: rays straight down in front, nearest first, from above the highest reachable ledge.
    std::optional<f32> top;
    for (f32 ahead = r + 0.1f; ahead <= r + reach + 1e-3f && !top; ahead += 0.1f)
    {
        const JPH::RVec3 from = feet + dir * ahead + JPH::Vec3(0.0f, maxHeight + 0.05f, 0.0f);
        const f32 length = maxHeight + 0.05f - minHeight + 0.05f;
        const auto hit = m.ray(from, JPH::Vec3(0.0f, -length, 0.0f));
        if (!hit)
        {
            continue;
        }
        const JPH::RVec3 point = from + JPH::Vec3(0.0f, -length * hit->mFraction, 0.0f);
        JPH::BodyLockRead lock(m.world->system.GetBodyLockInterface(), hit->mBodyID);
        if (lock.Succeeded() &&
            lock.GetBody().GetWorldSpaceSurfaceNormal(hit->mSubShapeID2, point).GetY() >= cosMaxSlope)
        {
            top = static_cast<f32>(point.GetY() - feet.GetY());
        }
    }
    if (!top || *top < minHeight || *top > maxHeight)
    {
        return std::nullopt;
    }
    // 2. The wall: a ray just below the top edge from the centre forwards.
    const JPH::RVec3 centre = feet + JPH::Vec3(0.0f, *top - 0.05f, 0.0f);
    const auto wall = m.ray(centre, dir * (r + reach));
    if (!wall)
    {
        return std::nullopt; // no edge in front (e.g. a slope rising there)
    }
    const f32 toWall = wall->mFraction * (r + reach);
    {
        // A wall, not a walkable slope (that one is walked up, not climbed).
        const JPH::RVec3 point = centre + dir * toWall;
        JPH::BodyLockRead lock(m.world->system.GetBodyLockInterface(), wall->mBodyID);
        if (!lock.Succeeded() ||
            lock.GetBody().GetWorldSpaceSurfaceNormal(wall->mSubShapeID2, point).GetY() >= cosMaxSlope)
        {
            return std::nullopt;
        }
    }
    // 3. Room: straight up at the current place, then on the top a little behind the edge.
    const JPH::RVec3 up = feet + JPH::Vec3(0.0f, *top, 0.0f);
    const JPH::RVec3 onTop = up + dir * (toWall + r + 0.05f);
    if (!m.roomAt(up) || !m.roomAt(onTop))
    {
        return std::nullopt;
    }
    const Vec3 target = fromJolt(JPH::Vec3(onTop));
    return Ledge{target + Vec3(0.0f, 0.01f, 0.0f), *top};
}

void CharacterController::teleport(const Vec3& feet)
{
    m_impl->character->SetPosition(JPH::RVec3(feet.x, feet.y, feet.z));
    m_impl->character->SetLinearVelocity(JPH::Vec3::sZero());
    m_impl->falling = false;
    m_impl->landing.reset();
    m_impl->refreshContacts(); // the ground state belongs to the new place right away
}

void CharacterController::setLimits(f32 maxSlopeDegrees, f32 stepHeight, f32 stickToFloor)
{
    m_impl->desc.maxSlopeDegrees = maxSlopeDegrees;
    m_impl->desc.stepHeight = stepHeight;
    m_impl->desc.stickToFloor = stickToFloor;
    m_impl->character->SetMaxSlopeAngle(JPH::DegreesToRadians(maxSlopeDegrees));
}

Vec3 CharacterController::feet() const
{
    return fromJolt(JPH::Vec3(m_impl->character->GetPosition()));
}

Vec3 CharacterController::visualFeet() const
{
    // Down from knee height at the centre; on flat ground that is feet() itself.
    const Vec3 f = feet();
    const f32 above = 0.5f;
    const f32 below = m_impl->desc.radius * 1.2f; // r tan 50 degrees = 0.36 m for r 0.3
    const JPH::RRayCast ray(JPH::RVec3(f.x, f.y + above, f.z), JPH::Vec3(0.0f, -(above + below), 0.0f));
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> hit;
    m_impl->world->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), hit, {}, m_impl->filter);
    if (!hit.HadHit() || state() == MoveState::Air)
    {
        return f;
    }
    return Vec3(f.x, f.y + above - hit.mHit.mFraction * (above + below), f.z);
}

Vec3 CharacterController::velocity() const
{
    return fromJolt(m_impl->character->GetLinearVelocity());
}

MoveState CharacterController::state() const
{
    switch (m_impl->character->GetGroundState())
    {
    case JPH::CharacterBase::EGroundState::OnGround:
        return MoveState::Ground;
    case JPH::CharacterBase::EGroundState::OnSteepGround:
        return MoveState::Slide;
    default:
        return MoveState::Air;
    }
}

Vec3 CharacterController::groundNormal() const
{
    return state() == MoveState::Air ? Vec3(0.0f, 1.0f, 0.0f)
                                     : fromJolt(m_impl->character->GetGroundNormal());
}

const CharacterDesc& CharacterController::desc() const
{
    return m_impl->desc;
}
} // namespace g7::physics

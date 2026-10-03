#include "JoltWorld.hpp"

#include <g7/physics/Character.hpp>

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>
#include <cmath>

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

    explicit Impl(PhysicsWorld::Impl& w, const CharacterDesc& d) : world(&w), desc(d), filter(d.collidesWith)
    {
    }

    void refreshContacts() { character->RefreshContacts({}, filter, {}, {}, *world->temp); }
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
    switch (c.GetGroundState())
    {
    case JPH::CharacterBase::EGroundState::OnGround:
        // Walking: the wanted velocity on top of what the ground does; gravity keeps the contact.
        velocity = c.GetGroundVelocity() + wanted + gravity * seconds;
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

    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mWalkStairsStepUp = JPH::Vec3(0.0f, m_impl->desc.stepHeight, 0.0f);
    update.mStickToFloorStepDown = JPH::Vec3(0.0f, -m_impl->desc.stickToFloor, 0.0f);
    c.ExtendedUpdate(seconds, gravity, update, {}, m_impl->filter, {}, {}, *m_impl->world->temp);
}

void CharacterController::teleport(const Vec3& feet)
{
    m_impl->character->SetPosition(JPH::RVec3(feet.x, feet.y, feet.z));
    m_impl->character->SetLinearVelocity(JPH::Vec3::sZero());
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

#pragma once

// Character controller (M5 part C): an upright cylinder (Character.cpp says why not a capsule) that
// walks on the static world - steps, slopes, sliding down slopes that are too steep, gravity. Built on Jolt
// CharacterVirtual (private). It knows no input: gameplay hands it the wanted horizontal velocity every fixed
// step. Spec: docs/modules/physics.md.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/physics/Physics.hpp>

#include <memory>
#include <optional>

namespace g7::physics
{
struct CharacterDesc
{
    f32 radius = 0.3f;        ///< m (human: physics.md)
    f32 height = 1.8f;        ///< m, total including both caps
    f32 maxSlopeDegrees = 50; ///< steeper ground: the character slides down and cannot walk up
    f32 stepHeight = 0.4f;    ///< m, steps up to this height are walked up
    f32 stickToFloor = 0.5f;  ///< m, walking down: stays on the ground over drops up to this
    LayerMask collidesWith = layerBit(Layer::World) | layerBit(Layer::Mob);
    u64 userData = 0; ///< reported by queries that hit the character (later: its vob id)
};

enum class MoveState : u8
{
    Ground, ///< standing on walkable ground
    Slide,  ///< on ground that is too steep: slides down, cannot move uphill
    Air,    ///< falling (or later jumping); no control over the horizontal velocity
};

/// A ledge the character can climb onto: where its feet will stand and how high that is above them.
struct Ledge
{
    Vec3 feet{0.0f};
    f32 height = 0.0f;
};

class CharacterController
{
public:
    CharacterController();
    ~CharacterController();
    CharacterController(CharacterController&&) noexcept;
    CharacterController& operator=(CharacterController&&) noexcept;

    /// A character with its feet at `feet`. `world` must outlive it; its static bodies may be rebuilt
    /// (PhysicsWorld::clear) while the character lives.
    [[nodiscard]] static Result<CharacterController> create(PhysicsWorld& world, const CharacterDesc& desc,
                                                            const Vec3& feet);

    /// One fixed step: on the ground the character takes `horizontalVelocity` (y ignored); on steep
    /// ground the uphill part of it is dropped and gravity pulls it down; in the air it keeps its
    /// horizontal velocity. Walks steps, follows the ground downhill.
    void update(f32 seconds, const Vec3& horizontalVelocity);
    /// Puts the feet at `feet` and stops all motion (start point, level change, debugging).
    void teleport(const Vec3& feet);
    /// Leaves the ground with this upward speed in the next update - only from walkable ground (state
    /// Ground); returns false otherwise. The horizontal velocity stays.
    bool jump(f32 upwardSpeed);
    /// Swimming and diving: moves with `velocity` (3D) without gravity, stair steps or floor snapping;
    /// walls and ground still collide. A fall in progress ends without a landing (the water catches it).
    void swim(f32 seconds, const Vec3& velocity);
    /// Moves the feet without collision and motion (climbing along a path that findLedge checked).
    /// teleport() at the end of the path settles the character again.
    void moveTo(const Vec3& feet);
    /// Height of the last fall, once, at landing: from the highest point in the air down to where it
    /// touched ground (walkable or steep). Sliding down a slope is no fall; jumps count from their top.
    [[nodiscard]] std::optional<f32> takeLanding();
    /// A ledge in `direction` (horizontal unit vector) whose top is between minHeight and maxHeight above
    /// the feet: a wall within `reach` of the character's side, a walkable top, and room for the whole
    /// character up there and on the way straight up.
    [[nodiscard]] std::optional<Ledge> findLedge(const Vec3& direction, f32 minHeight, f32 maxHeight,
                                                 f32 reach) const;
    /// Slope limit, step height and floor snapping (hot reload of movement data); size stays.
    void setLimits(f32 maxSlopeDegrees, f32 stepHeight, f32 stickToFloor);

    /// Bottom of the collision shape. On slopes it rests on its rim and the centre hovers a little.
    [[nodiscard]] Vec3 feet() const;
    /// Where to draw the feet: the ground straight below the centre (on flat ground = feet()).
    [[nodiscard]] Vec3 visualFeet() const;
    [[nodiscard]] Vec3 velocity() const;
    [[nodiscard]] MoveState state() const;
    /// Normal of the ground below (state Ground or Slide); +Y in the air.
    [[nodiscard]] Vec3 groundNormal() const;
    [[nodiscard]] const CharacterDesc& desc() const;
    [[nodiscard]] bool valid() const noexcept { return m_impl != nullptr; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::physics

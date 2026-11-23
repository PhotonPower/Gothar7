#pragma once

// Moving parts (docs/modules/dungeon.md D2): how a gate, secret wall or trapdoor moves, its course over time,
// and the geometry the engine needs to stop one on someone in its way (crush) and to close the waynet edges
// it blocks. Pure logic; the engine puts the poses into the scene, the drawing and the collision.

#include <g7/core/Geometry.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Transform.hpp>
#include <g7/world/Components.hpp>

#include <array>

namespace g7::world
{
/// The way of a mover from closed (0) to open (1): a slide and a turn, both in the vob's frame.
struct MoverMotion
{
    Vec3 offset{0.0f};           ///< metres, turned with the vob, not scaled
    Vec3 axis{1.0f, 0.0f, 0.0f}; ///< unit, through the vob's origin
    f32 angle = 0.0f;            ///< radians
};

/// The motion of `mover`: its kind's default worked out from the model's bounds (model space) and the vob's
/// scale - gate: up by the height; secret wall: back (-Z) by the depth; trapdoor: 90 degrees about +X, the
/// leaf (+Z) going down - with the file's `offset`, `axis` and `angle` in place of the default's parts.
[[nodiscard]] MoverMotion moverMotion(const Mover& mover, const AABB& modelBounds,
                                      const Vec3& scale) noexcept;

/// The vob's local transform at `position` (0 closed .. 1 open; eased at both ends).
[[nodiscard]] Transform moverPose(const Transform& closed, const MoverMotion& motion, f32 position) noexcept;

/// Where a mover is on its way and where it goes.
struct MoverCourse
{
    f32 position = 0.0f; ///< 0 closed .. 1 open
    f32 target = 0.0f;   ///< 0 or 1

    [[nodiscard]] bool moving() const noexcept { return position != target; }
    [[nodiscard]] bool closing() const noexcept { return target < position; }
    /// One step along the way that takes `wayTime` seconds in all; true when it arrives in this step.
    bool advance(f32 seconds, f32 wayTime) noexcept;
};

/// A box turned in space (a model's bounds placed in the world).
struct OrientedBox
{
    Vec3 center{0.0f};
    std::array<Vec3, 3> axes{Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)};
    Vec3 half{0.0f}; ///< along the axes, in metres
};

/// `local` (model space) placed by the affine `world` matrix (scale goes into the half sizes).
[[nodiscard]] OrientedBox orientedBox(const AABB& local, const Mat4& world) noexcept;
/// Distance from `point` to the box; 0 inside.
[[nodiscard]] f32 distanceTo(const OrientedBox& box, const Vec3& point) noexcept;
/// An upright capsule (its feet, radius and whole height) reaches into the box deeper than `margin`.
[[nodiscard]] bool capsuleInBox(const OrientedBox& box, const Vec3& feet, f32 radius, f32 height,
                                f32 margin = 0.05f) noexcept;
/// The line from `a` to `b` passes through the box.
[[nodiscard]] bool segmentCrossesBox(const OrientedBox& box, const Vec3& a, const Vec3& b) noexcept;
/// Whether a mover blocks the way at this course: gates and secret walls while not fully open, trapdoors
/// while not fully closed (the shaft is open).
[[nodiscard]] bool moverBlocks(Mover::Kind kind, const MoverCourse& course) noexcept;
} // namespace g7::world

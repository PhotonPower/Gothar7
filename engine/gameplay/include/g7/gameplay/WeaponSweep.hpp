// Weapon sweep (M11, docs/modules/gameplay.md "Kampf" - "Trefferprüfung"): during an attack's hit window the
// striking part of the weapon - a capsule from the grip along the blade, or a short one for the fist - is
// swept each fixed step from where it was to where the animation holds it now. Bodies it touches on the way
// are hit; a wall it meets stops it (the rest of the swing hits nothing). The engine asks the physics for the
// walls.
#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace g7::gameplay
{
/// The striking part of a weapon in one pose: the axis of a capsule from the grip to the tip (world space).
struct Blade
{
    Vec3 grip{0.0f};
    Vec3 tip{0.0f};
};

/// A body a blow can hit: an upright capsule from the feet up to `height`.
struct SweepTarget
{
    u32 id = 0;
    Vec3 feet{0.0f};
    f32 height = 1.8f;
    f32 radius = 0.35f;
};

/// Where the blade capsule (`radius`) at pose `from`, moved by `motion`, first meets something that stops a
/// swing: the share of `motion` in [0, 1] (0: it is in it already), nullopt when the way is free.
using BladeBlocker = std::function<std::optional<f32>(const Blade& from, f32 radius, const Vec3& motion)>;

struct SweepResult
{
    std::vector<u32> hits; ///< ids in the order the blade touched them, each once
    bool blocked = false;  ///< a wall stopped the blade
};

/// One fighter's swing in progress (the engine keeps one per fighter).
struct Swing
{
    std::vector<u32> hit;      ///< targets hit by this swing (each once)
    std::vector<Blade> blades; ///< the blades' poses in the last step of the hit window (fists: two)
    bool blocked = false;      ///< a wall stopped it: no more hits this swing

    void reset() noexcept
    {
        hit.clear();
        blades.clear();
        blocked = false;
    }
};

/// The pose between `a` (t = 0) and `b` (t = 1): the grip moves straight, the blade's direction turns, its
/// length changes evenly.
[[nodiscard]] Blade lerpBlade(const Blade& a, const Blade& b, f32 t) noexcept;

/// Shortest distance between the segments [p0, p1] and [q0, q1].
[[nodiscard]] f32 segmentDistance(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1) noexcept;

/// The blade capsule touches the body capsule.
[[nodiscard]] bool bladeTouches(const Blade& blade, f32 radius, const SweepTarget& target) noexcept;

/// One fixed step of a swing: the blade from pose `from` to pose `to`, in sub-steps of at most `maxSubStep`
/// metres of grip or tip travel (the turn of the blade within one is small). Each sub-step asks `blocker`
/// (may be empty: nothing blocks); targets are tested at every pose up to where the blade stopped. A start
/// pose inside a wall hits nothing. With `from` = `to` (the window's first step) only that pose is tested.
[[nodiscard]] SweepResult sweepBlade(const Blade& from, const Blade& to, f32 radius,
                                     std::span<const SweepTarget> targets, const BladeBlocker& blocker,
                                     f32 maxSubStep = 0.1f);
} // namespace g7::gameplay

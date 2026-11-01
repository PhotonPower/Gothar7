// Weapon sweep (M11, gameplay.md "Kampf" - "Trefferprüfung"): the blade capsule swept from pose to pose hits
// the bodies on its way - in its arc, not outside -, each once, and stops at a wall; knockback after a hit.

#include <g7/gameplay/Combat.hpp>
#include <g7/gameplay/WeaponSweep.hpp>
#include <g7/script/Value.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace g7;
using namespace g7::gameplay;

namespace
{
/// A sword of 0.9 m held at shoulder height 0.5 m right of the attacker at the origin, swung horizontally
/// from his right (+X) to his left: the blade's direction at `degrees` from +X towards -Z (ahead).
Blade swordAt(f32 degrees)
{
    const f32 a = glm::radians(degrees);
    const Vec3 grip(0.5f * std::cos(a), 1.3f, -0.5f * std::sin(a));
    const Vec3 direction(std::cos(a), 0.0f, -std::sin(a));
    return {grip, grip + direction * 0.9f};
}

/// The swing from `from` to `to` degrees in fixed steps of `step` degrees (as the engine's fixed steps do).
std::vector<u32> swing(f32 from, f32 to, f32 step, std::span<const SweepTarget> targets,
                       const BladeBlocker& blocker = {}, bool* blocked = nullptr)
{
    Swing s;
    for (f32 d = from; d <= to + 1e-3f; d += step)
    {
        const Blade now = swordAt(d);
        const SweepResult r = sweepBlade(s.blades.empty() ? now : s.blades[0], now, 0.08f, targets, blocker);
        s.blades = {now};
        for (const u32 id : r.hits)
        {
            if (std::ranges::find(s.hit, id) == s.hit.end()) // the engine keeps the swing's hits
            {
                s.hit.push_back(id);
            }
        }
        if (r.blocked)
        {
            if (blocked != nullptr)
            {
                *blocked = true;
            }
            break;
        }
    }
    return s.hit;
}
} // namespace

TEST_CASE("Weapon sweep: poses between two, segment distance, blade against a body")
{
    const Blade a{Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f)};
    const Blade b{Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, -2.0f)};
    const Blade mid = lerpBlade(a, b, 0.5f);
    CHECK(mid.grip.z == doctest::Approx(-0.5f));
    CHECK(glm::length(mid.tip - mid.grip) == doctest::Approx(1.0f)); // turned, not shortened
    CHECK(mid.tip.x == doctest::Approx(std::sqrt(0.5f)));

    CHECK(segmentDistance(Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(1, 1, 0), Vec3(1, 3, 0)) ==
          doctest::Approx(1.0f));
    CHECK(segmentDistance(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(3, 0, 0), Vec3(4, 0, 0)) ==
          doctest::Approx(2.0f));

    const SweepTarget body{1, Vec3(0.0f, 0.0f, -1.5f), 1.8f, 0.35f};
    CHECK(bladeTouches({Vec3(0, 1.3f, -0.5f), Vec3(0, 1.3f, -1.2f)}, 0.08f, body));
    CHECK_FALSE(bladeTouches({Vec3(0, 1.3f, -0.5f), Vec3(0, 1.3f, -1.0f)}, 0.08f, body)); // 0.5 m short
    CHECK_FALSE(bladeTouches({Vec3(0, 2.4f, -1.5f), Vec3(0, 3.0f, -1.5f)}, 0.08f, body)); // over the head
}

TEST_CASE("Weapon sweep: a swing hits a target in its arc, once, and none outside it")
{
    // Ahead (in the arc), behind and too far.
    const std::vector<SweepTarget> targets{{1, Vec3(0.0f, 0.0f, -1.5f), 1.8f, 0.35f},
                                           {2, Vec3(0.0f, 0.0f, 1.5f), 1.8f, 0.35f},
                                           {3, Vec3(0.0f, 0.0f, -3.0f), 1.8f, 0.35f}};
    // From the right over the front to the left, in steps of 15 degrees (fast: the blade jumps 0.3 m per
    // step).
    const std::vector<u32> hit = swing(0.0f, 180.0f, 15.0f, targets);
    REQUIRE(hit.size() == 1);
    CHECK(hit[0] == 1);

    // Only the first quarter (right to front-right): the target ahead is not reached yet.
    CHECK(swing(0.0f, 45.0f, 15.0f, targets).empty());

    // One long step (60 degrees, four times a fixed step of a fast swing) sweeps through the target in
    // sub-steps, and a target is hit once per call.
    const SweepResult r = sweepBlade(swordAt(60.0f), swordAt(120.0f), 0.08f, targets, {});
    REQUIRE(r.hits.size() == 1);
    CHECK(r.hits[0] == 1);
    CHECK_FALSE(r.blocked);
    // Without sub-steps the two end poses alone miss it: the sweep is what hits.
    CHECK_FALSE(bladeTouches(swordAt(60.0f), 0.08f, targets[0]));
    CHECK_FALSE(bladeTouches(swordAt(120.0f), 0.08f, targets[0]));
}

TEST_CASE(
    "Weapon sweep: a wall stops the blade - the target behind it is not hit, the rest of the swing neither")
{
    const std::vector<SweepTarget> targets{{1, Vec3(0.0f, 0.0f, -1.5f), 1.8f, 0.35f},
                                           {2, Vec3(-1.4f, 0.0f, 0.0f), 1.8f, 0.35f}};
    // A wall in the plane x = 0.3 for z < 0 (ahead right): the blade swinging from the right meets it. The
    // blocker tests the blade's points (stand-in for the physics' capsule cast): the share of the motion
    // where the first point crosses the plane.
    const BladeBlocker wall = [](const Blade& from, f32, const Vec3& motion) -> std::optional<f32>
    {
        std::optional<f32> first;
        for (f32 s = 0.0f; s <= 1.0f; s += 0.1f)
        {
            const Vec3 p = glm::mix(from.grip, from.tip, s);
            if (p.z < 0.0f && p.x <= 0.3f)
            {
                return 0.0f;
            }
            const Vec3 q = p + motion;
            if (q.z < 0.0f && q.x <= 0.3f && p.x > 0.3f && std::abs(q.x - p.x) > 1e-6f)
            {
                const f32 t = (p.x - 0.3f) / (p.x - q.x);
                first = first ? std::min(*first, t) : t;
            }
        }
        return first;
    };
    bool blocked = false;
    CHECK(swing(0.0f, 180.0f, 15.0f, targets, wall, &blocked).empty());
    CHECK(blocked);
    // Without the wall both are hit, in the order the blade reaches them.
    const std::vector<u32> free = swing(0.0f, 180.0f, 15.0f, targets);
    REQUIRE(free.size() == 2);
    CHECK(free[0] == 1);
    CHECK(free[1] == 2);
    // A start pose in the wall hits nothing, even a target it touches.
    const SweepResult inside = sweepBlade(swordAt(90.0f), swordAt(90.0f), 0.08f, targets, wall);
    CHECK(inside.blocked);
    CHECK(inside.hits.empty());
}

TEST_CASE("Knockback: a push over its time, then nothing")
{
    Knockback k;
    CHECK_FALSE(k.active());
    k.start(Vec3(3.0f, 1.0f, 4.0f), 0.3f, 0.2f); // made horizontal
    CHECK(k.active());
    f32 moved = 0.0f;
    Vec3 direction(0.0f);
    for (int i = 0; i < 30; ++i)
    {
        const Vec3 v = k.take(1.0f / 60.0f);
        CHECK(v.y == 0.0f);
        moved += glm::length(v) / 60.0f;
        direction += v;
    }
    CHECK(moved == doctest::Approx(0.3f).epsilon(0.01));
    CHECK(glm::normalize(direction).x == doctest::Approx(0.6f));
    CHECK_FALSE(k.active());
    k.start(Vec3(0.0f, 1.0f, 0.0f), 0.3f, 0.2f); // straight up: no push
    CHECK_FALSE(k.active());
}

TEST_CASE("Combat settings: the sweep's values from data/combat.lua, wrong ones are errors")
{
    script::Table t;
    t.fields["weapon_sweep"] = script::Value(true);
    t.fields["weapon_radius"] = script::Value(0.1);
    t.fields["knockback"] = script::Value(0.5);
    auto s = CombatSettings::fromTable(t);
    REQUIRE(s.ok());
    CHECK(s.value().weaponSweep);
    CHECK(s.value().weaponRadius == doctest::Approx(0.1f));
    CHECK(s.value().knockback == doctest::Approx(0.5f));
    CHECK(s.value().fistLength == doctest::Approx(0.12f)); // default kept
    CHECK(CombatSettings{}.weaponSweep);
    CHECK(CombatSettings{}.fistSweep);

    t.fields["fist_sweep"] = script::Value(false);
    REQUIRE(CombatSettings::fromTable(t).ok());
    CHECK_FALSE(CombatSettings::fromTable(t).value().fistSweep);
    t.fields["weapon_sweep"] = script::Value(1.0);
    CHECK_FALSE(CombatSettings::fromTable(t).ok());
    t.fields["weapon_sweep"] = script::Value(false);
    t.fields["knockback"] = script::Value(-1.0);
    CHECK_FALSE(CombatSettings::fromTable(t).ok());
}

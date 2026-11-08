#pragma once

// Telekinesis (M17, plan "Magie-Nachträge" part T, owner decisions 1-8 of 2026-11-08): the hero aims at an
// item, holds the spell, the item lifts and floats to his hand; held, it costs mana every second; let go, out
// of mana, out of reach or hit, it falls straight down and stays where it lands. No dynamic physics body: the
// flight is kinematic (deterministic); a sweep keeps it out of walls. Values from the Spell instance.

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>

#include <functional>
#include <optional>
#include <span>

namespace g7::gameplay
{
struct TelekinesisSettings
{
    f32 range = 15.0f;        ///< decision 2: metres from the eye to aim at an item
    f32 speed = 3.0f;         ///< decision 2: m/s floating towards the hand
    f32 lift = 0.5f;          ///< decision 2: metres it rises first
    f32 liftSpeed = 1.0f;     ///< m/s while rising
    f32 maxSize = 1.2f;       ///< decision 4: largest extent of an item that can be moved (metres)
    f32 arrive = 0.8f;        ///< metres from the hand: it is in the inventory
    f32 aimAngle = 20.0f;     ///< degrees from the aim within which an item is picked
    f32 leash = 1.5f;         ///< it falls when farther than range times this from the hand
    f32 gravity = 9.81f;      ///< falling
    f32 spin = 0.8f;          ///< radians per second about the vertical while carried
    f32 manaPerSecond = 2.0f; ///< decision 1
};

/// An item the hero might move: its vob id, the middle of its bounds and its largest extent.
struct TelekinesisCandidate
{
    u64 id = 0;
    Vec3 point{0.0f};
    f32 size = 0.0f;
    bool allowed = true; ///< false: the story forbids it (Item no_telekinesis)
};

/// The item to move: within aimAngle and range, small enough, allowed and visible (nothing in between; empty
/// = always visible); the one nearest to the aim, of two in nearly the same line the nearer one.
[[nodiscard]] std::optional<u64>
pickTelekinesisTarget(std::span<const TelekinesisCandidate> candidates, const Vec3& eye, const Vec3& aim,
                      const TelekinesisSettings& settings,
                      const std::function<bool(const TelekinesisCandidate&)>& visible = {});

/// Mana due for `seconds` more of holding at `perSecond`; `carry` keeps the fractions between steps.
[[nodiscard]] i32 telekinesisManaDue(f32& carry, f32 perSecond, f32 seconds) noexcept;

/// How far a sphere of the item's size can move from `from` along unit `direction` within `distance` (the
/// distance to the first wall; nullopt: free).
using TelekinesisSweep =
    std::function<std::optional<f32>(const Vec3& from, const Vec3& direction, f32 distance)>;

class TelekinesisFlight
{
public:
    enum class Phase : u8
    {
        Lift,    ///< rising above where it lay
        Float,   ///< towards the hand
        Arrived, ///< at the hand: into the inventory
        Falling, ///< let go
        Landed,  ///< on the ground
    };

    TelekinesisFlight(const Vec3& start, const TelekinesisSettings& settings) noexcept;

    /// One step while held: rise, then float towards `hand`. Returns the phase afterwards.
    Phase carry(f32 seconds, const Vec3& hand, const TelekinesisSweep& sweep = {});
    /// Let go: it falls straight down to `groundY`.
    void release(f32 groundY) noexcept;
    /// One step of falling (after release).
    Phase fall(f32 seconds) noexcept;
    /// Falls the rest of the way at once (a save while it falls).
    void land() noexcept;

    [[nodiscard]] Phase phase() const noexcept { return m_phase; }
    [[nodiscard]] const Vec3& position() const noexcept { return m_position; }
    /// Turn about the vertical so far (radians).
    [[nodiscard]] f32 spin() const noexcept { return m_spin; }
    /// Too far from the hand to hold (range times leash).
    [[nodiscard]] bool outOfReach(const Vec3& hand) const noexcept;

private:
    TelekinesisSettings m_settings;
    Vec3 m_position{0.0f};
    f32 m_liftTop = 0.0f;
    f32 m_spin = 0.0f;
    f32 m_fallSpeed = 0.0f;
    f32 m_groundY = 0.0f;
    Phase m_phase = Phase::Lift;
};
} // namespace g7::gameplay

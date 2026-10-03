#pragma once

// Focus (M8 part B, docs/modules/gameplay.md "Fokus"): what the player would act on - the candidate in a cone
// in front of the view, by kind priority NPC > Mob > Item, then by angle and distance, with hysteresis so the
// focus does not flicker between two neighbours. Values from assets/source/data/focus.toml.

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace g7::gameplay
{
enum class FocusKind : u8
{
    Npc,
    Mob,
    Item,
    Count,
};
[[nodiscard]] std::string_view focusKindName(FocusKind kind) noexcept; ///< "npc", "mob", "item"

struct FocusRange
{
    f32 distance = 3.0f; ///< metres from the eye to the focus point
    f32 angle = 30.0f;   ///< degrees between the view direction and the focus point, measured horizontally
};

struct FocusSettings
{
    std::array<FocusRange, static_cast<usize>(FocusKind::Count)> ranges{
        FocusRange{8.0f, 30.0f}, FocusRange{3.0f, 35.0f}, FocusRange{2.5f, 35.0f}};
    /// The current focus stays while it lies within distance and angle times this factor.
    f32 keep = 1.25f;
    /// Largest height difference between the eye and a focus point (metres).
    f32 height = 2.0f;

    [[nodiscard]] const FocusRange& range(FocusKind kind) const noexcept
    {
        return ranges[static_cast<usize>(kind)];
    }
    /// From TOML: [npc] / [mob] / [item] with distance and angle, top-level keep and height. Missing values
    /// keep the defaults.
    [[nodiscard]] static Result<FocusSettings> parse(std::string_view toml, std::string_view source);
};

struct FocusCandidate
{
    u64 id = 0; ///< VobId value (items, mobs) or the NPC's runtime id
    FocusKind kind = FocusKind::Item;
    Vec3 point{0.0f}; ///< where the focus name hangs and the angle is measured to
};

/// Whether nothing blocks the line from the eye to the candidate (world collision); empty = always visible.
using FocusVisible = std::function<bool(const FocusCandidate&)>;

/// The candidate in focus: the highest priority kind within its range, among those the best by angle and
/// distance; the `current` one wins while still within the widened range and no higher kind is in range.
[[nodiscard]] std::optional<u64> selectFocus(std::span<const FocusCandidate> candidates, const Vec3& eye,
                                             const Vec3& viewDirection, const FocusSettings& settings,
                                             std::optional<u64> current, const FocusVisible& visible = {});
} // namespace g7::gameplay

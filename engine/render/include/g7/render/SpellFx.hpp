#pragma once

// Spell glow (M17, plan "Magie-Nachträge" part G, owner decisions 9 and 10 of 2026-11-08): figures glow in a
// spell's colour while they cast it - hand and forearm, the outline faintly, stronger with each charged stage
// - and flash in the colour of the damage when a spell hits them; the burning glow orange, the sleeping
// shimmer faintly blue. Values from data/spell_fx.toml; the light is additive (the bloom makes the halo).

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace g7::render
{
/// Which arm glows: the one the cast clip raises (left, right) or both.
enum class GlowArm : u8
{
    Right = 1,
    Left = 2,
    Both = 3,
};

/// How strongly a glow lights a figure, in a colour (linear, may exceed 1 for the bloom).
struct GlowLook
{
    Vec3 color{0.0f};
    GlowArm arm = GlowArm::Left; ///< `arm` = "left" (the rune's and the mag clips' hand), "right" or "both"
    f32 hand = 0.0f;             ///< hand and forearm of the casting arm
    f32 rim = 0.0f;              ///< the outline (fresnel)
    f32 fill = 0.0f;             ///< the whole figure, flat
    f32 perStage = 0.0f;         ///< added to hand and rim per charged stage (as a factor: 0.5 = half again)
    f32 seconds = 0.0f;          ///< flashes: how long it fades out
    f32 pulse = 0.0f;            ///< states: pulses per second (0: steady)
};

/// data/spell_fx.toml, version 1: [glow.<spell>], [hit.<damage type>], [state.burning], [state.asleep].
struct SpellFxSettings
{
    std::map<std::string, GlowLook, std::less<>> spells;
    std::map<std::string, GlowLook, std::less<>> hits;
    GlowLook burning;
    GlowLook asleep;

    /// Errors name `source` and the key ("data/spell_fx.toml: glow.spl_x.color: needs [r, g, b]").
    [[nodiscard]] static Result<SpellFxSettings> parse(std::string_view toml, std::string_view source);
};

/// What one skinned draw adds (premultiplied colours; all zero: nothing, the image stays as it was).
struct FigureGlow
{
    Vec3 hand{0.0f};
    Vec3 rim{0.0f};
    Vec3 fill{0.0f};
    GlowArm arm = GlowArm::Left; ///< the arm `hand` lights
    /// Bones whose vertex weights count as "hand" (bit i of word i / 32): the engine fills them for `arm`.
    std::array<u32, 4> handBones{};

    [[nodiscard]] bool active() const noexcept
    {
        return glm::dot(hand, hand) + glm::dot(rim, rim) + glm::dot(fill, fill) > 0.0f;
    }
};

/// The flashes of one figure, fading out (hits); casting and states come in by FigureGlowState::glow.
class FigureGlowState
{
public:
    /// A hit in this look: it flashes for look.seconds (a newer, stronger one replaces it).
    void flash(const GlowLook& look) noexcept;
    void update(f32 seconds) noexcept;
    [[nodiscard]] bool flashing() const noexcept { return m_left > 0.0f; }
    /// All that lights the figure now: the spell it casts (null: none) with `stages` charged, burning,
    /// asleep, the flash. `time` (seconds) drives the pulse of a state.
    [[nodiscard]] FigureGlow glow(const GlowLook* casting, u32 stages, const GlowLook* state,
                                  f32 time) const noexcept;

private:
    GlowLook m_flash;
    f32 m_left = 0.0f;
};
} // namespace g7::render

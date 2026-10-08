#pragma once

// Facial animation (M6 part D): weights of the 15 face morph targets of characters-pipeline.md §6.1 -
// automatic blinking, expressions faded in and out, a rough lip movement while talking - or, while a voice
// plays (M13 D, owner decision 7), the mouth opened by the voice's loudness.

#include <g7/core/Types.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace g7::animation
{
/// The morph targets of a head, in the order of the contract (§6.1).
enum class FaceMorph : u8
{
    VisAa,
    VisEe,
    VisIh,
    VisOh,
    VisOu,
    VisMbp,
    VisFv,
    VisL,
    BlinkL,
    BlinkR,
    ExprAngry,
    ExprFriendly,
    ExprFear,
    ExprPain,
    ExprSleep,
    Count,
};
inline constexpr usize kFaceMorphCount = static_cast<usize>(FaceMorph::Count);

/// Contract name of a morph target ("vis_aa", "blink_l", "expr_sleep" ...).
[[nodiscard]] std::string_view faceMorphName(FaceMorph morph) noexcept;

/// Timing of the face (animgraph.toml `[face]`).
struct FaceSettings
{
    f32 blinkMinSeconds = 2.0f; ///< time between blinks, random in [min, max]
    f32 blinkMaxSeconds = 6.0f;
    f32 blinkSeconds = 0.15f;     ///< closing and opening together
    f32 visemesPerSecond = 8.0f;  ///< mouth shapes while talking
    f32 talkWeight = 0.8f;        ///< how far the mouth opens
    f32 expressionSeconds = 0.3f; ///< fade time of expressions
};

class FaceAnimator
{
public:
    explicit FaceAnimator(u32 seed = 1, const FaceSettings& settings = {});

    void setSettings(const FaceSettings& settings) noexcept { m_settings = settings; }
    /// "angry", "friendly", "fear", "pain", "sleep" (expr_*); weight 0..1, faded in over expressionSeconds.
    /// Other expressions fade out. Empty name or weight 0: neutral. False for an unknown name.
    bool setExpression(std::string_view name, f32 weight = 1.0f);
    [[nodiscard]] std::string_view expression() const noexcept;
    void setTalking(bool talking) noexcept { m_talking = talking; }
    [[nodiscard]] bool talking() const noexcept { return m_talking; }
    /// Lip sync from loudness: 0 (closed) .. 1 (open as far as talkWeight) drives vis_aa (and a little
    /// vis_oh) instead of the random mouth shapes, smoothed; nullopt hands the mouth back to setTalking.
    void setMouthOpen(std::optional<f32> open) noexcept { m_mouthTarget = open; }
    [[nodiscard]] bool mouthDriven() const noexcept { return m_mouthTarget.has_value(); }

    void update(f32 seconds);

    /// One weight per FaceMorph, for SkinnedMesh::setMorphWeights.
    [[nodiscard]] std::span<const f32> weights() const noexcept { return m_weights; }
    [[nodiscard]] f32 weight(FaceMorph morph) const noexcept { return m_weights[static_cast<usize>(morph)]; }

private:
    [[nodiscard]] f32 random01() noexcept;

    FaceSettings m_settings;
    u32 m_state = 1;
    std::array<f32, kFaceMorphCount> m_weights{};
    // Blinking: seconds until the next blink; >= 0 time into the running blink.
    f32 m_blinkWait = 0.0f;
    f32 m_blinkTime = -1.0f;
    // Expressions: target weight per expr_* morph.
    std::array<f32, 5> m_expressionTarget{};
    i32 m_expression = -1;
    // Talking: the viseme shown now and the one fading in.
    bool m_talking = false;
    f32 m_visemeTime = 0.0f;
    i32 m_visemeFrom = -1;
    i32 m_visemeTo = -1;
    f32 m_visemeWeightFrom = 0.0f;
    f32 m_visemeWeightTo = 0.0f;
    // Lip sync from loudness.
    std::optional<f32> m_mouthTarget;
    f32 m_mouth = 0.0f;
    f32 m_mouthPhase = 0.0f;
};
} // namespace g7::animation

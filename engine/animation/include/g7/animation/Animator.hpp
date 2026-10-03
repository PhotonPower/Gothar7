#pragma once

// Data-driven animation state machine (M6 part B): states play a clip or a 1D blend by a parameter,
// transitions switch on conditions and cross-fade, events fire, root motion is reported, an overlay
// layer plays over masked bones. Graph files: data/anim/<rig>.animgraph.toml (docs/modules/animation.md).

#include <g7/animation/Clip.hpp>
#include <g7/animation/Face.hpp>
#include <g7/animation/LookAt.hpp>
#include <g7/animation/Skeleton.hpp>
#include <g7/core/Result.hpp>

#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::animation
{
struct AnimCondition
{
    enum class Op : u8
    {
        Is,    ///< "air": bool true (or float != 0)
        IsNot, ///< "!air"
        Less,  ///< "speed < 1"
        LessEq,
        Greater,
        GreaterEq,
        Equal,
        NotEqual,
        End, ///< "end": the current state's clip has played through
    };
    std::string param;
    Op op = Op::Is;
    f32 value = 0.0f;
};

struct AnimGraphState
{
    std::string name;
    std::string blendParam;                          ///< empty: a single clip (points[0])
    std::vector<std::pair<f32, std::string>> points; ///< parameter value -> clip, rising
    f32 speed = 1.0f;
    bool rootMotion = false; ///< the root bone's movement is reported (rootMotion()) instead of drawn
    /// Parameter (m/s) the playback follows: rate = |value| / the clips' own speed (weighted in blends),
    /// within AnimGraph::rateRange - feet do not slide. Empty, or clips without a speed: rate 1.
    std::string rateParam;
};

struct AnimGraphTransition
{
    std::string from; ///< state name or "*" (any other state)
    std::string to;
    std::vector<AnimCondition> when; ///< all must hold
    f32 blend = 0.2f;                ///< cross-fade seconds
};

struct AnimGraph
{
    std::vector<std::string> sets; ///< VFS paths of the animation sets (glTF)
    std::string start;
    std::array<f32, 2> rateRange{0.6f, 1.8f}; ///< limits of the speed-matched playback rate
    FaceSettings face;                        ///< `[face]`: blinking, talking, expressions
    LookAtSettings lookAt;                    ///< `[look_at]`: bones and limits
    std::vector<AnimGraphState> states;
    std::vector<AnimGraphTransition> transitions;

    /// `<rig>.animgraph.toml`, version 1.
    [[nodiscard]] static Result<AnimGraph> parse(std::string_view toml, std::string_view source);
};

class Animator
{
public:
    /// Binds every clip the graph names (from `sets`) to `skeleton`; unknown clips and states are errors.
    [[nodiscard]] static Result<Animator> create(const AnimGraph& graph, const Skeleton& skeleton,
                                                 std::span<const asset::AnimationSetData* const> sets);

    void setFloat(std::string_view name, f32 value);
    void setBool(std::string_view name, bool value) { setFloat(name, value ? 1.0f : 0.0f); }
    [[nodiscard]] f32 param(std::string_view name) const;
    /// Switches to `state` now (blend seconds), e.g. after a teleport or for a scripted action.
    void enter(std::string_view state, f32 blend = 0.0f);

    /// Advances by `seconds`: at most one transition, then the pose; fires the events passed.
    void update(f32 seconds, const EventCallback& onEvent = {});

    /// Plays `clip` over the bones below `maskBone` ("spine_02": upper body) - weapons, gestures.
    void playOverlay(std::string_view clip, std::string_view maskBone, f32 blendIn = 0.15f,
                     bool additive = false);
    void stopOverlay(f32 blendOut = 0.15f);

    [[nodiscard]] const Pose& pose() const noexcept { return m_pose; }
    /// Movement of the root bone during the last update in a root-motion state (model space).
    [[nodiscard]] Vec3 rootMotion() const noexcept { return m_rootMotion; }
    [[nodiscard]] std::string_view state() const noexcept;
    [[nodiscard]] std::string_view previousState() const noexcept;
    [[nodiscard]] f32 fadeWeight() const noexcept; ///< 1 when no cross-fade runs
    /// Speed-matched playback rate of the current state (1 without `rate`).
    [[nodiscard]] f32 playbackRate() const;
    [[nodiscard]] f32 stateTime() const noexcept { return m_current.time; }
    /// Playback progress of the current state: 0..1 (one-shots), cycles (loops).
    [[nodiscard]] f32 stateProgress() const noexcept;
    [[nodiscard]] bool stateEnded() const noexcept;

    struct ClipWeight
    {
        std::string_view clip;
        f32 weight = 0.0f;
    };
    /// What shapes the pose now (debug UI).
    [[nodiscard]] std::vector<ClipWeight> activeClips() const;

private:
    struct StateDef
    {
        AnimGraphState def;
        std::vector<usize> clips; ///< indices into m_clips, parallel to def.points
    };
    struct Instance
    {
        i32 state = -1;
        f32 time = 0.0f; ///< seconds (single clip) or phase in cycles (blend)
        f32 previous = 0.0f;
    };
    struct Overlay
    {
        usize clip = 0;
        std::vector<f32> mask;
        bool additive = false;
        f32 time = 0.0f;
        f32 weight = 0.0f;
        f32 target = 1.0f;
        f32 rate = 10.0f;
    };

    [[nodiscard]] i32 findState(std::string_view name) const;
    [[nodiscard]] bool holds(const AnimCondition& c) const;
    [[nodiscard]] bool ended(const Instance& instance) const;
    void advance(Instance& instance, f32 seconds, const EventCallback* onEvent);
    void sample(const Instance& instance, Pose& pose) const;
    [[nodiscard]] std::vector<std::pair<usize, f32>> weights(const StateDef& state) const;
    [[nodiscard]] f32 rate(const StateDef& state) const;

    const Skeleton* m_skeleton = nullptr;
    std::vector<Clip> m_clips;
    std::vector<StateDef> m_states;
    std::vector<AnimGraphTransition> m_transitions;
    std::array<f32, 2> m_rateRange{0.6f, 1.8f};
    std::map<std::string, f32, std::less<>> m_params;
    Instance m_current;
    Instance m_previous;
    f32 m_fade = 1.0f;
    f32 m_fadeSeconds = 0.0f;
    std::optional<Overlay> m_overlay;
    Pose m_pose;
    Pose m_scratch;
    Vec3 m_rootMotion{0.0f};
};
} // namespace g7::animation

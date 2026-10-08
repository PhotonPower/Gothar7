#include <g7/animation/Animator.hpp>
#include <g7/core/Config.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace g7::animation
{
namespace
{
Error fail(std::string_view source, const std::string& what)
{
    return Error{std::string(source) + ": " + what};
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && s.front() == ' ')
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && s.back() == ' ')
    {
        s.remove_suffix(1);
    }
    return s;
}

/// "air", "!air", "end", "speed > 0.5" (ops < <= > >= == !=).
Result<AnimCondition> parseCondition(std::string_view text)
{
    text = trim(text);
    AnimCondition c;
    if (text == "end")
    {
        c.op = AnimCondition::Op::End;
        return c;
    }
    static constexpr std::pair<std::string_view, AnimCondition::Op> kOps[] = {
        {"<=", AnimCondition::Op::LessEq}, {">=", AnimCondition::Op::GreaterEq},
        {"==", AnimCondition::Op::Equal},  {"!=", AnimCondition::Op::NotEqual},
        {"<", AnimCondition::Op::Less},    {">", AnimCondition::Op::Greater}};
    for (const auto& [symbol, op] : kOps)
    {
        const usize at = text.find(symbol);
        if (at == std::string_view::npos)
        {
            continue;
        }
        c.param = std::string(trim(text.substr(0, at)));
        const std::string_view number = trim(text.substr(at + symbol.size()));
        const auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), c.value);
        if (c.param.empty() || ec != std::errc{} || end != number.data() + number.size())
        {
            return Error{std::format("condition '{}': expected '<param> {} <number>'", text, symbol)};
        }
        c.op = op;
        return c;
    }
    if (text.starts_with('!'))
    {
        c.op = AnimCondition::Op::IsNot;
        text.remove_prefix(1);
    }
    c.param = std::string(trim(text));
    if (c.param.empty() || c.param.find(' ') != std::string::npos)
    {
        return Error{std::format("condition '{}' is not understood", text)};
    }
    return c;
}
} // namespace

Result<AnimGraph> AnimGraph::parse(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    if (c.find<i64>("version") != 1)
    {
        return fail(source, "needs 'version = 1'");
    }
    AnimGraph graph;
    graph.sets = c.get<std::vector<std::string>>("sets", {});
    graph.start = c.get<std::string>("start", "");
    const auto range = c.get<std::vector<f64>>("rate_range", {0.6, 1.8});
    if (range.size() != 2 || !(range[0] > 0.0) || !(range[1] >= range[0]))
    {
        return fail(source, "'rate_range' needs [min, max] with 0 < min <= max");
    }
    graph.rateRange = {static_cast<f32>(range[0]), static_cast<f32>(range[1])};

    // [face] and [look_at]: optional, defaults from the structs.
    const auto number = [&](const std::string& key, f32 fallback)
    { return static_cast<f32>(c.get<f64>(key, static_cast<f64>(fallback))); };
    FaceSettings& face = graph.face;
    face.blinkMinSeconds = number("face.blink_min", face.blinkMinSeconds);
    face.blinkMaxSeconds = number("face.blink_max", face.blinkMaxSeconds);
    face.blinkSeconds = number("face.blink_seconds", face.blinkSeconds);
    face.visemesPerSecond = number("face.visemes_per_second", face.visemesPerSecond);
    face.talkWeight = number("face.talk_weight", face.talkWeight);
    face.expressionSeconds = number("face.expression_seconds", face.expressionSeconds);
    if (!(face.blinkMinSeconds > 0.0f) || face.blinkMaxSeconds < face.blinkMinSeconds ||
        !(face.blinkSeconds > 0.0f) || !(face.visemesPerSecond > 0.0f))
    {
        return fail(
            source,
            "[face]: blink_min > 0, blink_max >= blink_min, blink_seconds and visemes_per_second > 0");
    }
    LookAtSettings& look = graph.lookAt;
    if (const auto bones = c.find<std::vector<std::string>>("look_at.bones"))
    {
        const auto shares = c.get<std::vector<f64>>("look_at.shares", {});
        if (bones->empty() || shares.size() != bones->size())
        {
            return fail(source, "[look_at]: 'bones' and 'shares' need the same, non-zero length");
        }
        look.bones.clear();
        for (usize i = 0; i < bones->size(); ++i)
        {
            look.bones.emplace_back((*bones)[i], static_cast<f32>(shares[i]));
        }
    }
    look.maxYawDegrees = number("look_at.max_yaw", look.maxYawDegrees);
    look.maxPitchDegrees = number("look_at.max_pitch", look.maxPitchDegrees);
    look.behindDegrees = number("look_at.behind", look.behindDegrees);
    look.degreesPerSecond = number("look_at.degrees_per_second", look.degreesPerSecond);
    if (graph.sets.empty())
    {
        return fail(source, "needs 'sets' (animation set files)");
    }
    for (usize i = 0; i < c.arraySize("state"); ++i)
    {
        const std::string at = std::format("state[{}]", i);
        AnimGraphState state;
        state.name = c.get<std::string>(at + ".name", "");
        if (state.name.empty())
        {
            return fail(source, at + " needs a 'name'");
        }
        state.speed = static_cast<f32>(c.get<f64>(at + ".speed", 1.0));
        state.rootMotion = c.get<bool>(at + ".root_motion", false);
        state.rateParam = c.get<std::string>(at + ".rate", "");
        if (const auto clip = c.find<std::string>(at + ".clip"))
        {
            state.points.emplace_back(0.0f, *clip);
        }
        else
        {
            state.blendParam = c.get<std::string>(at + ".blend", "");
            for (usize p = 0; p < c.arraySize(at + ".points"); ++p)
            {
                const std::string point = std::format("{}.points[{}]", at, p);
                const auto value = c.find<f64>(point + ".value");
                const auto pointClip = c.find<std::string>(point + ".clip");
                if (!value || !pointClip)
                {
                    return fail(source, point + " needs 'value' and 'clip'");
                }
                if (!state.points.empty() && static_cast<f32>(*value) <= state.points.back().first)
                {
                    return fail(source, point + ": values must rise");
                }
                state.points.emplace_back(static_cast<f32>(*value), *pointClip);
            }
            if (state.blendParam.empty() || state.points.empty())
            {
                return fail(source,
                            std::format("state '{}' needs 'clip', or 'blend' with 'points'", state.name));
            }
        }
        graph.states.push_back(std::move(state));
    }
    for (usize i = 0; i < c.arraySize("transition"); ++i)
    {
        const std::string at = std::format("transition[{}]", i);
        AnimGraphTransition t;
        t.from = c.get<std::string>(at + ".from", "");
        t.to = c.get<std::string>(at + ".to", "");
        t.blend = static_cast<f32>(c.get<f64>(at + ".blend", 0.2));
        if (t.from.empty() || t.to.empty())
        {
            return fail(source, at + " needs 'from' and 'to'");
        }
        for (const std::string& when : c.get<std::vector<std::string>>(at + ".when", {}))
        {
            auto condition = parseCondition(when);
            if (!condition)
            {
                return fail(source, at + ": " + condition.error().message);
            }
            t.when.push_back(std::move(condition).value());
        }
        graph.transitions.push_back(std::move(t));
    }
    if (graph.states.empty())
    {
        return fail(source, "needs at least one [[state]]");
    }
    if (graph.start.empty())
    {
        graph.start = graph.states.front().name;
    }
    return graph;
}

Result<Animator> Animator::create(const AnimGraph& graph, const Skeleton& skeleton,
                                  std::span<const asset::AnimationSetData* const> sets)
{
    Animator a;
    a.m_skeleton = &skeleton;
    std::map<std::string, usize, std::less<>> clipIndex;
    for (const AnimGraphState& state : graph.states)
    {
        StateDef def{state, {}};
        for (const auto& [value, name] : state.points)
        {
            auto found = clipIndex.find(name);
            if (found == clipIndex.end())
            {
                const asset::ClipData* data = nullptr;
                for (const asset::AnimationSetData* set : sets)
                {
                    data = data != nullptr ? data : set->find(name);
                }
                if (data == nullptr)
                {
                    return Error{
                        std::format("animation: state '{}' plays unknown clip '{}'", state.name, name)};
                }
                found = clipIndex.emplace(name, a.m_clips.size()).first;
                a.m_clips.emplace_back(*data, skeleton);
            }
            def.clips.push_back(found->second);
        }
        a.m_states.push_back(std::move(def));
    }
    for (const AnimGraphTransition& t : graph.transitions)
    {
        if ((t.from != "*" && a.findState(t.from) < 0) || a.findState(t.to) < 0)
        {
            return Error{std::format("animation: transition {} -> {} names an unknown state", t.from, t.to)};
        }
    }
    a.m_transitions = graph.transitions;
    a.m_rateRange = graph.rateRange;
    a.m_current.state = a.findState(graph.start);
    if (a.m_current.state < 0)
    {
        return Error{std::format("animation: start state '{}' does not exist", graph.start)};
    }
    a.m_pose = skeleton.restPose();
    a.m_scratch = skeleton.restPose();
    a.update(0.0f);
    return a;
}

i32 Animator::findState(std::string_view name) const
{
    for (usize i = 0; i < m_states.size(); ++i)
    {
        if (m_states[i].def.name == name)
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

void Animator::setFloat(std::string_view name, f32 value)
{
    const auto it = m_params.find(name);
    if (it != m_params.end())
    {
        it->second = value;
    }
    else
    {
        m_params.emplace(std::string(name), value);
    }
}

f32 Animator::param(std::string_view name) const
{
    const auto it = m_params.find(name);
    return it != m_params.end() ? it->second : 0.0f;
}

void Animator::enter(std::string_view state, f32 blend)
{
    const i32 index = findState(state);
    if (index < 0 || index == m_current.state)
    {
        return;
    }
    m_previous = m_current;
    m_current = {index, 0.0f, 0.0f};
    m_fadeSeconds = blend;
    m_fade = blend > 0.0f ? 0.0f : 1.0f;
}

bool Animator::ended(const Instance& instance) const
{
    const StateDef& s = m_states[static_cast<usize>(instance.state)];
    if (!s.def.blendParam.empty())
    {
        return instance.time >= 1.0f; // one cycle of the blend
    }
    return instance.time >= m_clips[s.clips.front()].duration();
}

bool Animator::holds(const AnimCondition& c) const
{
    const f32 v = param(c.param);
    switch (c.op)
    {
    case AnimCondition::Op::Is:
        return v != 0.0f;
    case AnimCondition::Op::IsNot:
        return v == 0.0f;
    case AnimCondition::Op::Less:
        return v < c.value;
    case AnimCondition::Op::LessEq:
        return v <= c.value;
    case AnimCondition::Op::Greater:
        return v > c.value;
    case AnimCondition::Op::GreaterEq:
        return v >= c.value;
    case AnimCondition::Op::Equal:
        return std::abs(v - c.value) < 1e-4f;
    case AnimCondition::Op::NotEqual:
        return std::abs(v - c.value) >= 1e-4f;
    case AnimCondition::Op::End:
        return ended(m_current);
    }
    return false;
}

std::vector<std::pair<usize, f32>> Animator::weights(const StateDef& state) const
{
    // Single clip, or the two neighbours of the parameter on the blend line.
    if (state.def.blendParam.empty() || state.clips.size() == 1)
    {
        return {{state.clips.front(), 1.0f}};
    }
    const f32 v = param(state.def.blendParam);
    const auto& points = state.def.points;
    if (v <= points.front().first)
    {
        return {{state.clips.front(), 1.0f}};
    }
    if (v >= points.back().first)
    {
        return {{state.clips.back(), 1.0f}};
    }
    for (usize i = 1; i < points.size(); ++i)
    {
        if (v <= points[i].first)
        {
            const f32 t = (v - points[i - 1].first) / (points[i].first - points[i - 1].first);
            return {{state.clips[i - 1], 1.0f - t}, {state.clips[i], t}};
        }
    }
    return {{state.clips.back(), 1.0f}};
}

void Animator::advance(Instance& instance, f32 seconds, const EventCallback* onEvent)
{
    const StateDef& s = m_states[static_cast<usize>(instance.state)];
    const f32 dt = seconds * s.def.speed * rate(s);
    instance.previous = instance.time;
    if (s.def.blendParam.empty())
    {
        const Clip& clip = m_clips[s.clips.front()];
        instance.time += dt;
        if (onEvent != nullptr)
        {
            clip.fireEvents(instance.previous, instance.time, *onEvent);
        }
        return;
    }
    // Blend: a shared phase (cycles), so steps of walk and run stay in step; the cycle length follows the
    // weights. Events come from the clip with the larger weight.
    const auto w = weights(s);
    f32 cycle = 0.0f;
    for (const auto& [clip, weight] : w)
    {
        cycle += m_clips[clip].duration() * weight;
    }
    instance.time += cycle > 0.0f ? dt / cycle : 0.0f;
    if (onEvent != nullptr)
    {
        const auto dominant =
            std::max_element(w.begin(), w.end(), [](auto& a, auto& b) { return a.second < b.second; });
        const Clip& clip = m_clips[dominant->first];
        clip.fireEvents(instance.previous * clip.duration(), instance.time * clip.duration(), *onEvent);
    }
}

f32 Animator::rate(const StateDef& s) const
{
    if (s.def.rateParam.empty())
    {
        return 1.0f;
    }
    // The clips' own speed, weighted like the pose; clips without one (standing) do not count.
    f32 native = 0.0f;
    f32 total = 0.0f;
    const auto add = [&](usize clip, f32 weight)
    {
        if (m_clips[clip].nativeSpeed() > 0.0f)
        {
            native += m_clips[clip].nativeSpeed() * weight;
            total += weight;
        }
    };
    if (s.def.blendParam.empty())
    {
        add(s.clips.front(), 1.0f);
    }
    else
    {
        for (const auto& [clip, weight] : weights(s))
        {
            add(clip, weight);
        }
    }
    if (total <= 0.0f)
    {
        return 1.0f;
    }
    return std::clamp(std::abs(param(s.def.rateParam)) / (native / total), m_rateRange[0], m_rateRange[1]);
}

f32 Animator::playbackRate() const
{
    return m_current.state < 0 ? 1.0f : rate(m_states[static_cast<usize>(m_current.state)]);
}

void Animator::sample(const Instance& instance, Pose& pose) const
{
    const StateDef& s = m_states[static_cast<usize>(instance.state)];
    if (s.def.blendParam.empty())
    {
        pose = m_skeleton->restPose();
        m_clips[s.clips.front()].sample(instance.time, pose);
        return;
    }
    bool first = true;
    Pose part = m_skeleton->restPose();
    for (const auto& [clip, weight] : weights(s))
    {
        const Clip& c = m_clips[clip];
        if (first)
        {
            pose = m_skeleton->restPose();
            c.sample(instance.time * c.duration(), pose);
            first = false;
            continue;
        }
        part = m_skeleton->restPose();
        c.sample(instance.time * c.duration(), part);
        blendPose(pose, part, weight);
    }
}

void Animator::update(f32 seconds, const EventCallback& onEvent)
{
    // 1. At most one transition (file order wins).
    for (const AnimGraphTransition& t : m_transitions)
    {
        const bool from = t.from == "*" ? m_states[static_cast<usize>(m_current.state)].def.name != t.to
                                        : m_states[static_cast<usize>(m_current.state)].def.name == t.from;
        if (from &&
            std::all_of(t.when.begin(), t.when.end(), [&](const AnimCondition& c) { return holds(c); }))
        {
            enter(t.to, t.blend);
            break;
        }
    }

    // 2. Time.
    advance(m_current, seconds, onEvent ? &onEvent : nullptr);
    if (m_fade < 1.0f)
    {
        advance(m_previous, seconds, nullptr);
        m_fade = m_fadeSeconds > 0.0f ? std::min(1.0f, m_fade + seconds / m_fadeSeconds) : 1.0f;
    }

    // 3. Root motion: in root-motion states the root's movement and turn are reported (blends: weighted,
    // loops across their ends), the drawn root stays put.
    m_rootMotion = Vec3(0.0f);
    m_rootYaw = 0.0f;
    const StateDef& current = m_states[static_cast<usize>(m_current.state)];
    const i32 root = m_skeleton->find("root");
    if (current.def.rootMotion)
    {
        if (current.def.blendParam.empty())
        {
            const Clip::RootMotion m =
                m_clips[current.clips.front()].rootMotion(m_current.previous, m_current.time);
            m_rootMotion = m.translation;
            m_rootYaw = m.yaw;
        }
        else
        {
            for (const auto& [clip, weight] : weights(current))
            {
                const Clip& c = m_clips[clip];
                const Clip::RootMotion m =
                    c.rootMotion(m_current.previous * c.duration(), m_current.time * c.duration());
                m_rootMotion += m.translation * weight;
                m_rootYaw += m.yaw * weight;
            }
        }
    }
    const auto holdRoot = [&](const Instance& instance, Pose& pose)
    {
        const StateDef& s = m_states[static_cast<usize>(instance.state)];
        if (!s.def.rootMotion || root < 0)
        {
            return;
        }
        BoneTransform& r = pose[static_cast<usize>(root)];
        r.translation = m_skeleton->restPose()[static_cast<usize>(root)].translation;
        if (s.def.blendParam.empty()) // turns: the drawn root keeps facing ahead
        {
            const Clip& c = m_clips[s.clips.front()];
            const f32 t = c.loops() && c.duration() > 0.0f ? std::fmod(instance.time, c.duration())
                                                           : std::min(instance.time, c.duration());
            r.rotation = glm::angleAxis(-c.rootYaw(t), Vec3(0.0f, 1.0f, 0.0f)) * r.rotation;
        }
    };

    // 4. Pose: previous state fading out under the current one, then the overlay.
    sample(m_current, m_pose);
    holdRoot(m_current, m_pose);
    if (m_fade < 1.0f && m_previous.state >= 0)
    {
        sample(m_previous, m_scratch);
        holdRoot(m_previous, m_scratch);
        blendPose(m_scratch, m_pose, m_fade);
        std::swap(m_scratch, m_pose);
    }
    if (m_overlay)
    {
        Overlay& o = *m_overlay;
        const Clip& clip = m_clips[o.clip];
        const f32 before = o.time;
        o.time += seconds;
        if (onEvent)
        {
            clip.fireEvents(before, o.time, onEvent);
        }
        if (!clip.loops() && o.time >= clip.duration())
        {
            o.target = 0.0f; // played through: fade out
        }
        o.weight = o.target > o.weight ? std::min(o.target, o.weight + o.rate * seconds)
                                       : std::max(o.target, o.weight - o.rate * seconds);
        m_scratch = m_skeleton->restPose();
        clip.sample(o.time, m_scratch);
        if (o.additive)
        {
            Pose reference = m_skeleton->restPose();
            (o.reference >= 0 ? m_clips[static_cast<usize>(o.reference)] : clip).sample(0.0f, reference);
            addPose(m_pose, m_scratch, reference, o.weight, o.mask);
        }
        else
        {
            blendPose(m_pose, m_scratch, o.weight, o.mask);
        }
        if (o.target == 0.0f && o.weight <= 0.0f)
        {
            m_overlay.reset();
        }
    }
}

void Animator::playOverlay(std::string_view clip, std::string_view maskBone, f32 blendIn, bool additive,
                           std::string_view reference)
{
    i32 referenceClip = -1;
    for (usize i = 0; i < m_clips.size() && !reference.empty(); ++i)
    {
        if (m_clips[i].name() == reference)
        {
            referenceClip = static_cast<i32>(i);
        }
    }
    for (usize i = 0; i < m_clips.size(); ++i)
    {
        if (m_clips[i].name() == clip)
        {
            Overlay o;
            o.clip = i;
            o.mask = m_skeleton->maskBelow(maskBone);
            o.additive = additive;
            o.reference = referenceClip;
            o.rate = blendIn > 0.0f ? 1.0f / blendIn : 1e6f;
            m_overlay = std::move(o);
            return;
        }
    }
}

bool Animator::hasClip(std::string_view clip) const noexcept
{
    return std::ranges::any_of(m_clips, [&](const Clip& c) { return c.name() == clip; });
}

void Animator::stopOverlay(f32 blendOut)
{
    if (m_overlay)
    {
        m_overlay->target = 0.0f;
        m_overlay->rate = blendOut > 0.0f ? 1.0f / blendOut : 1e6f;
    }
}

std::string_view Animator::state() const noexcept
{
    return m_states[static_cast<usize>(m_current.state)].def.name;
}

std::string_view Animator::previousState() const noexcept
{
    return m_previous.state >= 0 ? std::string_view(m_states[static_cast<usize>(m_previous.state)].def.name)
                                 : std::string_view();
}

f32 Animator::fadeWeight() const noexcept
{
    return m_fade;
}

f32 Animator::stateProgress() const noexcept
{
    const StateDef& s = m_states[static_cast<usize>(m_current.state)];
    if (!s.def.blendParam.empty())
    {
        return m_current.time;
    }
    const f32 d = m_clips[s.clips.front()].duration();
    return d > 0.0f ? m_current.time / d : 1.0f;
}

bool Animator::stateEnded() const noexcept
{
    return ended(m_current);
}

std::vector<Animator::ClipWeight> Animator::activeClips() const
{
    std::vector<ClipWeight> out;
    const auto add = [&](const Instance& instance, f32 scale)
    {
        if (instance.state < 0 || scale <= 0.0f)
        {
            return;
        }
        for (const auto& [clip, weight] : weights(m_states[static_cast<usize>(instance.state)]))
        {
            out.push_back({m_clips[clip].name(), weight * scale});
        }
    };
    add(m_current, m_fade);
    if (m_fade < 1.0f)
    {
        add(m_previous, 1.0f - m_fade);
    }
    if (m_overlay)
    {
        out.push_back({m_clips[m_overlay->clip].name(), m_overlay->weight});
    }
    return out;
}
} // namespace g7::animation

namespace g7::animation
{
usize applyVariant(AnimGraph& graph, std::string_view variant,
                   std::span<const asset::AnimationSetData* const> sets)
{
    if (variant.empty())
    {
        return 0;
    }
    const auto find = [&](std::string_view name) -> const asset::ClipData*
    {
        for (const asset::AnimationSetData* set : sets)
        {
            if (const asset::ClipData* clip = set->find(name))
            {
                return clip;
            }
        }
        return nullptr;
    };
    usize replaced = 0;
    for (AnimGraphState& state : graph.states)
    {
        bool moved = false;
        for (auto& [value, name] : state.points)
        {
            const std::string own = std::format("{}_{}", name, variant);
            const asset::ClipData* base = find(name);
            const asset::ClipData* other = find(own);
            if (other == nullptr)
            {
                continue;
            }
            // A different own speed (the old one's slow trot): its point moves to that speed.
            if (!state.blendParam.empty() && value > 0.0f && base != nullptr && base->speed > 0.0f &&
                other->speed > 0.0f && std::abs(other->speed - base->speed) > 1e-3f)
            {
                value = other->speed;
                moved = true;
            }
            name = own;
            ++replaced;
        }
        if (moved)
        {
            std::ranges::sort(state.points, {}, [](const auto& p) { return p.first; });
        }
    }
    return replaced;
}
} // namespace g7::animation

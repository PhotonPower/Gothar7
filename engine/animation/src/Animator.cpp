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
    const f32 dt = seconds * s.def.speed;
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

    // 3. Root motion: in root-motion states the root's movement is reported, the drawn root stays put.
    m_rootMotion = Vec3(0.0f);
    const StateDef& current = m_states[static_cast<usize>(m_current.state)];
    const i32 root = m_skeleton->find("root");
    if (current.def.rootMotion && current.def.blendParam.empty())
    {
        const Clip& clip = m_clips[current.clips.front()];
        const f32 to = std::min(m_current.time, clip.duration());
        const f32 from = std::min(m_current.previous, clip.duration());
        m_rootMotion = clip.rootTranslation(to) - clip.rootTranslation(from);
    }

    // 4. Pose: previous state fading out under the current one, then the overlay.
    sample(m_current, m_pose);
    if (current.def.rootMotion && root >= 0)
    {
        m_pose[static_cast<usize>(root)].translation =
            m_skeleton->restPose()[static_cast<usize>(root)].translation;
    }
    if (m_fade < 1.0f && m_previous.state >= 0)
    {
        sample(m_previous, m_scratch);
        if (m_states[static_cast<usize>(m_previous.state)].def.rootMotion && root >= 0)
        {
            m_scratch[static_cast<usize>(root)].translation =
                m_skeleton->restPose()[static_cast<usize>(root)].translation;
        }
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
            clip.sample(0.0f, reference);
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

void Animator::playOverlay(std::string_view clip, std::string_view maskBone, f32 blendIn, bool additive)
{
    for (usize i = 0; i < m_clips.size(); ++i)
    {
        if (m_clips[i].name() == clip)
        {
            m_overlay = Overlay{i,    m_skeleton->maskBelow(maskBone),       additive, 0.0f, 0.0f,
                                1.0f, blendIn > 0.0f ? 1.0f / blendIn : 1e6f};
            return;
        }
    }
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

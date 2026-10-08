// Dynamic music (M13 part C): definitions, the choice of a set, the state's hysteresis and the player that
// chains segments on the mixer's clock.

#include <g7/audio/Music.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace g7::audio
{
namespace
{
constexpr std::array<std::string_view, 3> kStateNames = {"std", "thr", "fgt"};
constexpr std::string_view kCommon = "common";
constexpr f64 kLeadSeconds = 0.05; ///< planned this far ahead of the mixer

/// Whole frames of `count` units of `unit` frames after `start`, at or after `from`.
u64 nextGrid(u64 start, u64 unit, u64 from) noexcept
{
    if (from <= start || unit == 0)
    {
        return start;
    }
    const u64 steps = (from - start + unit - 1) / unit;
    return start + steps * unit;
}
} // namespace

std::string_view musicStateName(MusicState state) noexcept
{
    const auto i = static_cast<usize>(state);
    return i < kStateNames.size() ? kStateNames[i] : std::string_view("?");
}

std::optional<MusicState> musicStateFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < kStateNames.size(); ++i)
    {
        if (kStateNames[i] == name)
        {
            return static_cast<MusicState>(i);
        }
    }
    return std::nullopt;
}

Result<MusicDefs> parseMusicDefs(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    MusicDefs defs;
    defs.threatStates = c.get<std::vector<std::string>>("threat_states", {});
    defs.fightStates = c.get<std::vector<std::string>>("fight_states", {});
    defs.threatRange = static_cast<f32>(c.get<f64>("threat_range", defs.threatRange));
    defs.fightRange = static_cast<f32>(c.get<f64>("fight_range", defs.fightRange));
    defs.fightMemory = static_cast<f32>(c.get<f64>("fight_memory", defs.fightMemory));
    defs.hysteresis = static_cast<f32>(c.get<f64>("hysteresis", defs.hysteresis));
    for (const std::string& name : c.get<std::vector<std::string>>("outside", {}))
    {
        const auto state = musicStateFromName(name);
        if (!state)
        {
            return Error{std::format("{}: outside: unknown state \"{}\" (std, thr, fgt)", source, name)};
        }
        defs.outside.push_back(*state);
    }

    const auto parseSet = [&](const std::string& table) -> Result<MusicSet>
    {
        MusicSet set;
        set.segments = c.get<std::vector<std::string>>(table + ".segments", {});
        if (set.segments.empty())
        {
            return Error{std::format("{}: {}: segments are missing", source, table)};
        }
        set.intro = c.get<std::string>(table + ".intro", "");
        const std::string transition = c.get<std::string>(table + ".transition", "next_bar");
        if (transition == "end")
        {
            set.transition = MusicTransition::End;
        }
        else if (transition != "next_bar")
        {
            return Error{std::format("{}: {}: transition is \"next_bar\" or \"end\"", source, table)};
        }
        set.fade = static_cast<f32>(c.get<f64>(table + ".fade", set.fade));
        set.bpm = static_cast<f32>(c.get<f64>(table + ".bpm", 0.0));
        set.beats = static_cast<u32>(std::max<i64>(0, c.get<i64>(table + ".beats", 0)));
        if (set.fade < 0.0f || set.bpm < 0.0f)
        {
            return Error{std::format("{}: {}: fade and bpm must not be below 0", source, table)};
        }
        return set;
    };

    for (const std::string& name : c.keys("theme"))
    {
        const std::string base = "theme." + name;
        MusicTheme theme;
        theme.bpm = static_cast<f32>(c.get<f64>(base + ".bpm", theme.bpm));
        theme.beats = static_cast<u32>(c.get<i64>(base + ".beats", theme.beats));
        theme.volume = static_cast<f32>(c.get<f64>(base + ".volume", theme.volume));
        if (theme.bpm <= 0.0f || theme.beats == 0)
        {
            return Error{std::format("{}: theme {}: bpm and beats must be above 0", source, name)};
        }
        for (const std::string& key : c.keys(base))
        {
            if (key == "bpm" || key == "beats" || key == "volume")
            {
                continue;
            }
            if (!musicStateFromName(key))
            {
                return Error{
                    std::format("{}: theme {}: unknown key \"{}\" (std, thr, fgt)", source, name, key)};
            }
            const std::string table = base + "." + key;
            for (const std::string& sub : c.keys(table))
            {
                if (sub == "day" || sub == "ngt")
                {
                    auto set = parseSet(table + "." + sub);
                    if (!set)
                    {
                        return set.error();
                    }
                    theme.sets.emplace(key + "." + sub, std::move(set.value()));
                }
                else if (sub != "segments" && sub != "intro" && sub != "transition" && sub != "fade" &&
                         sub != "bpm" && sub != "beats")
                {
                    return Error{std::format("{}: {}: unknown key \"{}\"", source, table, sub)};
                }
            }
            if (c.contains(table + ".segments"))
            {
                auto set = parseSet(table);
                if (!set)
                {
                    return set.error();
                }
                theme.sets.emplace(key, std::move(set.value()));
            }
        }
        if (theme.sets.empty())
        {
            return Error{std::format("{}: theme {} has no music", source, name)};
        }
        defs.themes.emplace(name, std::move(theme));
    }
    for (const std::string& name : c.keys("stinger"))
    {
        MusicStinger s;
        s.files = c.get<std::vector<std::string>>("stinger." + name + ".files", {});
        s.volume = static_cast<f32>(c.get<f64>("stinger." + name + ".volume", 1.0));
        if (s.files.empty())
        {
            return Error{std::format("{}: stinger {}: files are missing", source, name)};
        }
        defs.stingers.emplace(name, std::move(s));
    }
    return defs;
}

std::optional<MusicChoice> chooseMusic(const MusicDefs& defs, std::string_view theme, MusicState state,
                                       bool night)
{
    // No zone, or a theme without music: silence - but for the states played everywhere (common's).
    const bool inZone = !theme.empty() && theme != kCommon && defs.themes.contains(theme);
    for (i32 s = static_cast<i32>(state); s >= 0; --s)
    {
        if (!inZone && std::ranges::find(defs.outside, static_cast<MusicState>(s)) == defs.outside.end())
        {
            continue;
        }
        const std::string name(musicStateName(static_cast<MusicState>(s)));
        const std::string keys[] = {name + (night ? ".ngt" : ".day"), name};
        for (const std::string_view owner : {inZone ? theme : kCommon, kCommon})
        {
            const auto t = defs.themes.find(owner);
            if (t == defs.themes.end())
            {
                continue;
            }
            for (const std::string& key : keys)
            {
                if (const auto set = t->second.sets.find(key); set != t->second.sets.end())
                {
                    return MusicChoice{t->first, key, &t->second, &set->second};
                }
            }
        }
    }
    return std::nullopt;
}

f64 MusicChoice::barSeconds() const noexcept
{
    const f64 bpm = set->bpm > 0.0f ? set->bpm : theme->bpm;
    return 60.0 * beats() / bpm;
}

MusicState MusicStateFilter::update(MusicState raw, f32 seconds, f32 hold) noexcept
{
    if (raw >= m_state)
    {
        m_state = raw;
        m_calm = 0.0f;
        return m_state;
    }
    m_calm += seconds;
    if (m_calm >= hold)
    {
        m_state = raw;
        m_calm = 0.0f;
    }
    return m_state;
}

MusicPlayer::MusicPlayer(MusicDefs defs, u32 seed) : m_defs(std::move(defs)), m_rng(seed)
{
}

std::vector<std::string> MusicPlayer::files() const
{
    std::vector<std::string> out;
    for (const auto& [name, theme] : m_defs.themes)
    {
        for (const auto& [key, set] : theme.sets)
        {
            out.insert(out.end(), set.segments.begin(), set.segments.end());
            if (!set.intro.empty())
            {
                out.push_back(set.intro);
            }
        }
    }
    for (const auto& [name, stinger] : m_defs.stingers)
    {
        out.insert(out.end(), stinger.files.begin(), stinger.files.end());
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::optional<MusicPlayer::Segment> MusicPlayer::start(AudioSystem& audio, const MusicChoice& choice, u64 at,
                                                       bool intro)
{
    const MusicSet& set = *choice.set;
    std::string file;
    if (intro)
    {
        file = set.intro;
    }
    else
    {
        // At random, not the one just played (with a choice).
        std::vector<const std::string*> pool;
        for (const std::string& s : set.segments)
        {
            if (set.segments.size() == 1 || !m_current || s != m_current->file)
            {
                pool.push_back(&s);
            }
        }
        file = *pool[std::uniform_int_distribution<usize>(0, pool.size() - 1)(m_rng)];
    }
    const u64 frames = audio.clipFrames(file);
    if (frames == 0)
    {
        if (std::ranges::find(m_missing, file) == m_missing.end())
        {
            G7_LOG_WARN("audio", "music: {} is not loaded", file);
            m_missing.push_back(file);
        }
        return std::nullopt;
    }
    SoundDef def;
    def.files = {file};
    def.volume = choice.theme->volume;
    def.bus = Bus::Music;
    auto id = audio.playAtFrame(def, at);
    if (!id)
    {
        G7_LOG_WARN("audio", "music: {}", id.error().message);
        return std::nullopt;
    }
    const f64 rate = audio.sampleRate();
    Segment s;
    s.id = id.value();
    s.file = file;
    s.set = choice.id();
    s.start = at;
    s.end = at + frames;
    s.bar = static_cast<u64>(std::llround(choice.barSeconds() * rate));
    s.beat = std::max<u64>(1, s.bar / choice.beats());
    s.intro = intro;
    return s;
}

void MusicPlayer::queueNext(AudioSystem& audio, const MusicChoice& choice)
{
    if (m_current && !m_next)
    {
        m_next = start(audio, choice, m_current->end, false);
    }
}

void MusicPlayer::update(AudioSystem& audio, std::string_view theme, MusicState state, bool night)
{
    const u64 now = audio.frame();
    const u64 lead = static_cast<u64>(std::llround(kLeadSeconds * audio.sampleRate()));
    m_status.theme = std::string(theme);
    m_status.state = state;
    m_status.night = night;
    if (m_status.changeAt != 0 && now >= m_status.changeAt)
    {
        m_status.changeAt = 0;
    }
    // The queued segment takes over at its start; without one the music has run out.
    if (m_next && now >= m_next->start)
    {
        m_current = std::move(m_next);
        m_next.reset();
    }
    else if (m_current && !m_next && now >= m_current->end)
    {
        m_current.reset();
    }

    const std::optional<MusicChoice> choice = chooseMusic(m_defs, theme, state, night);
    const std::string target = choice ? choice->id() : std::string();
    if (m_current && m_current->set == target)
    {
        queueNext(audio, *choice);
    }
    else if (!m_current)
    {
        if (choice)
        {
            // From silence: at once - or where the last music has faded out.
            m_current =
                start(audio, *choice, std::max(now + lead, m_fadingUntil), !choice->set->intro.empty());
            queueNext(audio, *choice);
        }
    }
    else
    {
        // A change: on the next bar of what plays (or at its end); it fades out up to there.
        const MusicSet* to = choice ? choice->set : nullptr;
        u64 at = to != nullptr && to->transition == MusicTransition::End
                     ? m_current->end
                     : std::min(nextGrid(m_current->start, m_current->bar, now + lead), m_current->end);
        if (m_next)
        {
            audio.stop(m_next->id);
            m_next.reset();
        }
        audio.stopAtFrame(m_current->id, at, to != nullptr ? to->fade : 1.0f);
        m_status.changeAt = at;
        if (choice)
        {
            m_current = start(audio, *choice, at, !choice->set->intro.empty());
            queueNext(audio, *choice);
        }
        else
        {
            m_current.reset();
            m_fadingUntil = at;
        }
    }
    m_status.set = m_current ? m_current->set : std::string();
    m_status.segment = m_current ? m_current->file : std::string();
    m_status.segmentStart = m_current ? m_current->start : 0;
}

Result<void> MusicPlayer::stinger(AudioSystem& audio, std::string_view name)
{
    const auto it = m_defs.stingers.find(name);
    if (it == m_defs.stingers.end())
    {
        return Error{std::format("no stinger \"{}\"", name)};
    }
    const u64 now = audio.frame() + static_cast<u64>(std::llround(kLeadSeconds * audio.sampleRate()));
    const u64 at = m_current ? nextGrid(m_current->start, m_current->beat, now) : now;
    SoundDef def;
    def.files = it->second.files;
    def.volume = it->second.volume;
    def.bus = Bus::Music;
    auto id = audio.playAtFrame(def, at);
    if (!id)
    {
        return id.error();
    }
    m_status.stinger = std::string(name);
    return {};
}

void MusicPlayer::stop(AudioSystem& audio, f32 fadeSeconds)
{
    if (m_next)
    {
        audio.stop(m_next->id);
    }
    if (m_current)
    {
        audio.stop(m_current->id, fadeSeconds);
    }
    m_current.reset();
    m_next.reset();
    m_status.set.clear();
    m_status.segment.clear();
    m_status.changeAt = 0;
}
} // namespace g7::audio

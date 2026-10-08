// Dynamic music (M13 part C, owner decisions 4 and 5): the smallest music box around the hero gives the theme
// (none: silence, owner), the state comes from real enemies - an NPC in one of data/music.toml's threat
// states that sees the hero threatens, one in a fight state fighting him (fight_target(npc) == "hero" in the
// scripts) or the hero's own blows given and taken mean a fight - with 5 s hysteresis on the way down; 20:00
// - 6:00 is night. The audio module's MusicPlayer changes on bar boundaries.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>

namespace g7
{
namespace
{
using script::Value;

constexpr f32 kCauseInterval = 0.25f; ///< s between looks at the enemies around
constexpr f32 kZoneHeight = 1.0f;     ///< m above the feet: the point the music zone is taken at
} // namespace

void Engine::initMusic()
{
    const std::string path = m_config.settings.get<std::string>("game.music", "data/music.toml");
    auto bytes = m_vfs.read(path);
    if (!bytes)
    {
        G7_LOG_WARN("engine", "music: {}", bytes.error().message);
        return;
    }
    auto defs = audio::parseMusicDefs(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()), path);
    if (!defs)
    {
        G7_LOG_WARN("engine", "{}", defs.error().message);
        return;
    }
    m_music.emplace(std::move(defs).value(), m_config.randomSeed.value_or(0x5eed));
    usize loaded = 0;
    for (const std::string& file : m_music->files())
    {
        auto clip = m_vfs.read(file);
        if (!clip)
        {
            G7_LOG_WARN("engine", "music: {}", clip.error().message);
            continue;
        }
        if (auto added = m_audio->addClip(file, clip.value()); !added)
        {
            G7_LOG_WARN("engine", "{}", added.error().message);
            continue;
        }
        ++loaded;
    }
    G7_LOG_INFO("engine", "music: {} themes, {} stingers, {} files", m_music->defs().themes.size(),
                m_music->defs().stingers.size(), loaded);
}

void Engine::noteHeroFight()
{
    if (m_music)
    {
        m_musicFightSeconds = m_music->defs().fightMemory;
    }
}

audio::MusicState Engine::musicCause()
{
    if (m_musicFightSeconds > 0.0f)
    {
        return audio::MusicState::Fgt;
    }
    const audio::MusicDefs& defs = m_music->defs();
    const auto listed = [](const std::vector<std::string>& list, std::string_view state)
    { return std::ranges::find(list, state) != list.end(); };
    audio::MusicState cause = audio::MusicState::Std;
    for (const auto& c : m_creatures)
    {
        if (c->dead || c->vanished || c->asleep || !c->seesPlayer || c->state.empty())
        {
            continue;
        }
        const f32 distance = glm::length(c->position - m_playerFeet);
        if (listed(defs.fightStates, c->state) && distance <= defs.fightRange)
        {
            // Only a fight against the hero (guards fighting a wolf next to him are no threat to him).
            bool atHero = true;
            if (m_scripts && m_scripts->global("fight_target").isFunction())
            {
                const Value args[] = {c->species};
                auto target = m_scripts->callGlobal("fight_target", args);
                atHero = target && target.value().isString() && target.value().asString() == "hero";
            }
            if (atHero)
            {
                return audio::MusicState::Fgt;
            }
        }
        if (listed(defs.threatStates, c->state) && distance <= defs.threatRange)
        {
            cause = audio::MusicState::Thr;
        }
    }
    return cause;
}

void Engine::updateMusic(f32 seconds)
{
    if (!m_audio || !m_music)
    {
        return;
    }
    m_musicFightSeconds = std::max(0.0f, m_musicFightSeconds - seconds);
    m_musicCauseTimer -= seconds;
    if (m_musicCauseTimer <= 0.0f)
    {
        m_musicCauseTimer = kCauseInterval;
        m_musicCause = musicCause();
    }
    const audio::MusicState state =
        m_musicForcedState.value_or(m_musicFilter.update(m_musicCause, seconds, m_music->defs().hysteresis));
    const std::string theme = m_musicForcedTheme.value_or(
        zoneAt("music", m_playerFeet + Vec3(0.0f, kZoneHeight, 0.0f)).value_or(std::string()));
    m_music->update(*m_audio, theme, state, night());
    const std::string now = m_music->status().set;
    if (now != m_musicSet)
    {
        m_musicSet = now;
        G7_LOG_DEBUG("engine", "music: {} ({} {})", now.empty() ? "silence" : now, theme,
                     audio::musicStateName(state));
        if (m_scripts)
        {
            const Value args[] = {theme, std::string(audio::musicStateName(state))};
            m_scripts->emit("music_changed", args);
        }
    }
}

void Engine::bindMusicFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"music_state", "music_state() -> {theme, state, night, set, segment, stinger}",
             "Was die Musik gerade spielt (M13): Thema der Musik-Zone (leer: Stille), Zustand std/thr/fgt, "
             "Nacht, gespielte Menge (Thema/Schlüssel, z. B. common/fgt), Datei und der letzte Stinger.",
             "Klang", [this](std::span<const Value>) -> Result<Value>
             {
                 if (!m_music)
                 {
                     return Value();
                 }
                 const auto& s = m_music->status();
                 return script::makeTable({}, {{"theme", s.theme},
                                               {"state", std::string(audio::musicStateName(s.state))},
                                               {"night", s.night},
                                               {"set", s.set},
                                               {"segment", s.segment},
                                               {"stinger", s.stinger}});
             }});
    vm.bind(
        {"music_stinger", "music_stinger(name: string) -> boolean",
         "Spielt einen Stinger aus data/music.toml auf dem nächsten Schlag über der Musik (gelöste Quest, "
         "Stufenaufstieg, Tod, neues Kapitel).",
         "Klang", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"expects (name)"};
             }
             if (!m_music || !m_audio)
             {
                 return Value(false);
             }
             if (auto played = m_music->stinger(*m_audio, a[0].asString()); !played)
             {
                 return played.error();
             }
             return Value(true);
         }});
    vm.bind(
        {"music_force", "music_force(theme?: string, state?: string)",
         "Zum Testen: erzwingt Thema (\"\" = Stille) und Zustand (std, thr, fgt); ohne Argumente gilt wieder "
         "die Welt.",
         "Klang", [this](std::span<const Value> a) -> Result<Value>
         {
             m_musicForcedTheme.reset();
             m_musicForcedState.reset();
             if (!a.empty() && a[0].isString())
             {
                 m_musicForcedTheme = a[0].asString();
             }
             if (a.size() > 1 && a[1].isString())
             {
                 const auto state = audio::musicStateFromName(a[1].asString());
                 if (!state)
                 {
                     return Error{std::format("unknown music state \"{}\" (std, thr, fgt)", a[1].asString())};
                 }
                 m_musicForcedState = *state;
             }
             return Value();
         }});
    vm.bind({"music_changed",
             "on(\"music_changed\", fn(theme: string, state: string))",
             "Die Musik wechselt (Thema bzw. Zustand); Thema leer: Stille.",
             "Ereignisse",
             {}});
}
} // namespace g7

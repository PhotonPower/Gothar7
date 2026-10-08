// Voices (M13 part D, owner decisions 2026-10-08): a dialogue line or a shout is spoken when its chosen take
// exists - voice/<language>/<key>.wav (WAV for now; OGG with real takes and an ADR) - as a 3D sound at the
// speaker's head, full up to 4 m. The line then lasts as long as the take plus a pause, the subtitles follow
// the voice; without a take only the subtitles show (reading time from the text). While a voice plays its
// loudness opens the speaker's mouth (lip sync from loudness, decision 7) and the music ducks by 6 dB.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7
{
namespace
{
using script::Value;

constexpr f32 kEnvelopeHz = 30.0f;     ///< loudness windows per second (lip sync)
constexpr f32 kVoiceHead = 1.6f;       ///< m above the feet: where a voice sounds
constexpr f32 kVoiceFull = 4.0f;       ///< m: full volume up to here (owner)
constexpr f32 kVoiceSilent = 40.0f;    ///< m: silent from here
constexpr f32 kLinePause = 0.3f;       ///< s after a spoken line before the next
constexpr f32 kDuckFadeSeconds = 0.3f; ///< the music ducks and comes back over this long (owner)
constexpr std::string_view kHero = "hero";
} // namespace

std::string Engine::voiceFile(std::string_view key) const
{
    if (key.empty())
    {
        return {};
    }
    const std::string language = m_config.settings.get<std::string>("voice.language", "de");
    for (const char* ext : {"wav", "ogg"})
    {
        std::string path = std::format("voice/{}/{}.{}", language, key, ext);
        if (m_vfs.exists(path))
        {
            return path;
        }
    }
    return {};
}

std::optional<f32> Engine::speak(std::string_view speaker, std::string_view key)
{
    if (!m_audio)
    {
        return std::nullopt;
    }
    const std::string file = voiceFile(key);
    if (file.empty())
    {
        return std::nullopt;
    }
    if (!m_audio->hasClip(file))
    {
        auto bytes = m_vfs.read(file);
        if (!bytes)
        {
            G7_LOG_WARN("engine", "voice {}: {}", key, bytes.error().message);
            return std::nullopt;
        }
        if (auto added = m_audio->addClip(file, bytes.value()); !added)
        {
            G7_LOG_WARN("engine", "voice {}: {}", key, added.error().message);
            return std::nullopt;
        }
    }
    stopVoice(speaker); // one voice per speaker
    const std::optional<Vec3> at = speakerHead(speaker);
    audio::SoundDef def;
    def.files = {file};
    def.bus = audio::Bus::Voice;
    def.minDistance = kVoiceFull;
    def.maxDistance = kVoiceSilent;
    auto played = m_audio->play(def, at);
    if (!played)
    {
        G7_LOG_WARN("engine", "{}", played.error().message);
        return std::nullopt;
    }
    SpokenVoice v;
    v.speaker = std::string(speaker);
    v.key = std::string(key);
    v.sound = played.value();
    v.envelope = m_audio->clipEnvelope(file, kEnvelopeHz);
    v.seconds = m_audio->clipSeconds(file);
    m_voices.push_back(std::move(v));
    if (m_scripts)
    {
        const Value args[] = {std::string(speaker), std::string(key),
                              static_cast<f64>(m_voices.back().seconds)};
        m_scripts->emit("voice_line", args);
    }
    return m_voices.back().seconds + kLinePause;
}

void Engine::stopVoice(std::string_view speaker)
{
    for (auto it = m_voices.begin(); it != m_voices.end();)
    {
        if (speaker.empty() || it->speaker == speaker)
        {
            if (m_audio)
            {
                m_audio->stop(it->sound, 0.05f);
            }
            if (animation::FaceAnimator* face = speakerFace(it->speaker))
            {
                face->setMouthOpen(std::nullopt);
            }
            it = m_voices.erase(it);
            continue;
        }
        ++it;
    }
}

bool Engine::voicePlaying(std::string_view speaker) const
{
    return std::ranges::any_of(m_voices, [&](const SpokenVoice& v) { return v.speaker == speaker; });
}

std::optional<Vec3> Engine::speakerHead(std::string_view speaker) const
{
    if (speaker == kHero)
    {
        return m_playerFeet + Vec3(0.0f, kVoiceHead, 0.0f);
    }
    for (const auto& c : m_creatures)
    {
        if (c->species == speaker)
        {
            return c->position + Vec3(0.0f, kVoiceHead, 0.0f);
        }
    }
    return std::nullopt;
}

animation::FaceAnimator* Engine::speakerFace(std::string_view speaker)
{
    if (speaker == kHero)
    {
        return m_figure ? &m_figure->face : nullptr;
    }
    for (const auto& c : m_creatures)
    {
        if (c->species == speaker && c->figure)
        {
            return &c->figure->face;
        }
    }
    return nullptr;
}

void Engine::updateVoices(f32 seconds)
{
    if (!m_audio)
    {
        return;
    }
    for (auto it = m_voices.begin(); it != m_voices.end();)
    {
        animation::FaceAnimator* face = speakerFace(it->speaker);
        const std::optional<f32> at = m_audio->position(it->sound);
        if (!at)
        {
            if (face != nullptr)
            {
                face->setMouthOpen(std::nullopt); // done: the mouth closes
            }
            it = m_voices.erase(it);
            continue;
        }
        // The mouth follows the loudness at the playing position; the voice follows the speaker.
        if (face != nullptr && !it->envelope.empty())
        {
            const auto index = std::min(it->envelope.size() - 1, static_cast<usize>(*at * kEnvelopeHz));
            face->setMouthOpen(it->envelope[index]);
        }
        if (const auto head = speakerHead(it->speaker))
        {
            m_audio->setPosition(it->sound, *head);
        }
        ++it;
    }
    // Ducking: the music 6 dB down while anyone speaks (owner), faded.
    const auto duck = static_cast<f32>(std::clamp(m_config.settings.get<f64>("audio.duck", 0.5), 0.0, 1.0));
    const f32 target = m_voices.empty() ? 1.0f : duck;
    const f32 step = seconds / kDuckFadeSeconds;
    m_musicDuck =
        m_musicDuck < target ? std::min(target, m_musicDuck + step) : std::max(target, m_musicDuck - step);
    m_audio->setBusVolume(audio::Bus::Music,
                          static_cast<f32>(m_config.settings.get<f64>("audio.music", 1.0)) * m_musicDuck);
}

f32 Engine::faceWeight(std::string_view who, std::string_view morph)
{
    const animation::FaceAnimator* face = speakerFace(who);
    for (usize i = 0; face != nullptr && i < animation::kFaceMorphCount; ++i)
    {
        if (animation::faceMorphName(static_cast<animation::FaceMorph>(i)) == morph)
        {
            return face->weight(static_cast<animation::FaceMorph>(i));
        }
    }
    return 0.0f;
}

void Engine::bindVoiceFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"voice_playing", "voice_playing(npc: string) -> boolean",
             "Ob die Stimme eines NPCs (oder \"hero\") gerade spricht (M13: gewählter Take vorhanden).",
             "Klang", [this](std::span<const Value> a) -> Result<Value>
             { return Value(!a.empty() && a[0].isString() && voicePlaying(a[0].asString())); }});
    vm.bind({"voice_line",
             "on(\"voice_line\", fn(npc: string, key: string, seconds: number))",
             "Eine Zeile wird gesprochen (es gibt ihren Take voice/<sprache>/<key>.wav).",
             "Ereignisse",
             {}});
}
} // namespace g7

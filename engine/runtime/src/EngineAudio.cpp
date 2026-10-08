// Sound (M13 part A, ADR 0007): the mixer of the audio module, sounds as data (data/sounds.toml), their clips
// read from the VFS on first use; the listener follows the camera. Headless (tests, CI) there is no output
// device: the engine's frame time renders the mixer, so tests hear the same each run. Volumes from
// engine.toml [audio].

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <format>

namespace g7
{
namespace
{
using script::Value;
} // namespace

void Engine::initAudio()
{
    const Config& settings = m_config.settings;
    if (!settings.get<bool>("audio.enabled", true))
    {
        G7_LOG_INFO("engine", "audio switched off ([audio] enabled = false)");
        return;
    }
    audio::AudioConfig config;
    config.device = !m_config.headless && settings.get<bool>("audio.device", true);
    config.sampleRate = static_cast<u32>(settings.get<i64>("audio.sample_rate", 48000));
    config.seed = m_config.randomSeed.value_or(0x5eed);
    auto created = audio::AudioSystem::create(config);
    if (!created && config.device)
    {
        G7_LOG_WARN("engine", "{} - continuing without sound output", created.error().message);
        config.device = false;
        created = audio::AudioSystem::create(config);
    }
    if (!created)
    {
        G7_LOG_WARN("engine", "{}", created.error().message);
        return;
    }
    m_audio = std::move(created).value();
    m_audio->setMasterVolume(static_cast<f32>(settings.get<f64>("audio.master", 1.0)));
    for (u8 b = 0; b < static_cast<u8>(audio::Bus::Count); ++b)
    {
        const auto bus = static_cast<audio::Bus>(b);
        m_audio->setBusVolume(
            bus, static_cast<f32>(settings.get<f64>(std::format("audio.{}", audio::busName(bus)), 1.0)));
    }
    const std::string path = settings.get<std::string>("game.sounds", "data/sounds.toml");
    auto bytes = m_vfs.read(path);
    if (!bytes)
    {
        G7_LOG_WARN("engine", "sounds: {}", bytes.error().message);
        return;
    }
    auto defs = audio::parseSoundDefs(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()), path);
    if (!defs)
    {
        G7_LOG_WARN("engine", "{}", defs.error().message);
        return;
    }
    m_soundDefs = std::move(defs).value();
    G7_LOG_INFO("engine", "audio: {} sounds", m_soundDefs.size());
}

void Engine::updateAudio(f64 realSeconds)
{
    if (!m_audio)
    {
        return;
    }
    const Vec3 forward = m_camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    m_audio->setListener(m_camera.transform.position, forward);
    m_audio->update(static_cast<f32>(realSeconds));
}

std::optional<audio::SoundId> Engine::playSound(std::string_view name, std::optional<Vec3> position)
{
    if (!m_audio)
    {
        return std::nullopt;
    }
    const auto def = m_soundDefs.find(name);
    if (def == m_soundDefs.end())
    {
        G7_LOG_DEBUG("engine", "sound {}: not in data/sounds.toml", name);
        return std::nullopt;
    }
    for (const std::string& file : def->second.files)
    {
        if (m_audio->hasClip(file))
        {
            continue;
        }
        auto bytes = m_vfs.read(file);
        if (!bytes)
        {
            G7_LOG_WARN("engine", "sound {}: {}", name, bytes.error().message);
            return std::nullopt;
        }
        if (auto added = m_audio->addClip(file, bytes.value()); !added)
        {
            G7_LOG_WARN("engine", "{}", added.error().message);
            return std::nullopt;
        }
    }
    auto played = m_audio->play(def->second, position);
    if (!played)
    {
        G7_LOG_WARN("engine", "{}", played.error().message);
        return std::nullopt;
    }
    ++m_soundsPlayed[std::string(name)];
    return played.value();
}

void Engine::bindAudioFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind(
        {"sound", "sound(name: string, x?: number, y?: number, z?: number) -> integer | nil",
         "Spielt einen Klang aus data/sounds.toml (M13), mit Ort als 3D-Klang; gibt seine Nummer zurück, nil "
         "wenn es ihn nicht gibt bzw. kein Ton läuft.",
         "Klang", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"expects (name, x?, y?, z?)"};
             }
             std::optional<Vec3> at;
             if (a.size() >= 4 && a[1].isNumber() && a[2].isNumber() && a[3].isNumber())
             {
                 at = Vec3(static_cast<f32>(a[1].asNumber()), static_cast<f32>(a[2].asNumber()),
                           static_cast<f32>(a[3].asNumber()));
             }
             const auto id = playSound(a[0].asString(), at);
             return id ? Value(static_cast<i64>(*id)) : Value();
         }});
    vm.bind({"sound_stop", "sound_stop(id: integer, fade?: number)",
             "Beendet einen Klang, über `fade` Sekunden ausgeblendet.", "Klang",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isNumber())
                 {
                     return Error{"expects (id, fade?)"};
                 }
                 if (m_audio)
                 {
                     m_audio->stop(static_cast<audio::SoundId>(a[0].asInteger()),
                                   a.size() > 1 ? static_cast<f32>(a[1].asNumber(0.0)) : 0.0f);
                 }
                 return Value();
             }});
    vm.bind({"sound_playing", "sound_playing(id: integer) -> boolean", "Ob ein Klang noch läuft.", "Klang",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 return Value(m_audio && !a.empty() && a[0].isNumber() &&
                              m_audio->playing(static_cast<audio::SoundId>(a[0].asInteger())));
             }});
}
} // namespace g7

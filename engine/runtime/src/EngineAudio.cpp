// Sound (M13 part A, ADR 0007): the mixer of the audio module, sounds as data (data/sounds.toml), their clips
// read from the VFS on first use; the listener follows the camera. Headless (tests, CI) there is no output
// device: the engine's frame time renders the mixer, so tests hear the same each run. Volumes from
// engine.toml [audio].

#include <g7/core/Log.hpp>
#include <g7/physics/Physics.hpp>
#include <g7/runtime/Engine.hpp>

#include <cmath>
#include <format>
#include <random>

namespace g7
{
namespace
{
using script::Value;

constexpr f32 kNightFrom = 20.0f;        ///< hours: night ambience and music from here ...
constexpr f32 kNightTo = 6.0f;           ///< ... until here
constexpr f32 kOcclusionInterval = 0.1f; ///< s between the occlusion rays
constexpr f32 kOccludedMuffle = 0.8f;    ///< a wall between: this muffled (owner decision 8: a low-pass)
constexpr std::string_view kIndoorAmbience = "innen"; ///< in a room (indoor zone): this ambience ...
constexpr f32 kOutsideVolume = 0.3f;                  ///< ... with the outside one this loud ...
constexpr f32 kOutsideMuffle = 0.8f;                  ///< ... and this muffled behind it

/// Whether `p` lies in the turned box (world.md: local +X = (cos yaw, 0, -sin yaw), +Z = (sin yaw, 0, cos
/// yaw)).
bool inBox(const world::ZoneBox& b, const Vec3& p)
{
    const Vec3 d = p - b.center;
    const f32 yaw = glm::radians(b.yawDegrees);
    const f32 c = std::cos(yaw);
    const f32 s = std::sin(yaw);
    const Vec3 local(d.x * c - d.z * s, d.y, d.x * s + d.z * c);
    return std::abs(local.x) <= b.halfExtents.x && std::abs(local.y) <= b.halfExtents.y &&
           std::abs(local.z) <= b.halfExtents.z;
}
} // namespace

const world::Zone* Engine::smallestZoneAt(std::string_view type, const Vec3& point, f32* volume) const
{
    // Nested zones: the smallest box wins (a camp inside a forest).
    const world::Zone* best = nullptr;
    f32 bestVolume = 0.0f;
    for (const world::Zone& z : m_worldFile.zones)
    {
        if (z.type != type || !z.box || !inBox(*z.box, point))
        {
            continue;
        }
        const f32 v = z.box->halfExtents.x * z.box->halfExtents.y * z.box->halfExtents.z;
        if (best == nullptr || v < bestVolume)
        {
            best = &z;
            bestVolume = v;
        }
    }
    if (volume != nullptr)
    {
        *volume = bestVolume;
    }
    return best;
}

std::optional<std::string> Engine::zoneAt(std::string_view type, const Vec3& point) const
{
    const world::Zone* z = smallestZoneAt(type, point, nullptr);
    return z != nullptr ? std::optional<std::string>(z->value) : std::nullopt;
}

bool Engine::night() const noexcept
{
    const f32 hour = m_gameTime.hourOfDay();
    return hour >= kNightFrom || hour < kNightTo;
}

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
    // Ambiences (M13 part B).
    const std::string ambientPath = settings.get<std::string>("game.ambient", "data/ambient.toml");
    if (auto ambientBytes = m_vfs.read(ambientPath); ambientBytes)
    {
        auto ambient = audio::parseAmbientDefs(
            std::string_view(reinterpret_cast<const char*>(ambientBytes.value().data()),
                             ambientBytes.value().size()),
            ambientPath);
        if (ambient)
        {
            m_ambientDefs = std::move(ambient).value();
        }
        else
        {
            G7_LOG_WARN("engine", "{}", ambient.error().message);
        }
    }
    G7_LOG_INFO("engine", "audio: {} sounds, {} ambiences", m_soundDefs.size(), m_ambientDefs.size());
    initMusic(); // M13 part C
}

void Engine::updateAudio(f64 realSeconds)
{
    if (!m_audio)
    {
        return;
    }
    const Vec3 forward = m_camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    m_audio->setListener(m_camera.transform.position, forward);
    updateAmbience(static_cast<f32>(realSeconds));
    updateOcclusion(static_cast<f32>(realSeconds));
    updateMusic(static_cast<f32>(realSeconds));
    m_audio->update(static_cast<f32>(realSeconds));
}

std::optional<audio::SoundId> Engine::startAmbienceLoop(std::string_view ambience, bool isNight, f32 volume,
                                                        f32 muffle)
{
    const auto def = m_ambientDefs.find(ambience);
    if (def == m_ambientDefs.end())
    {
        return std::nullopt;
    }
    const std::string& loop =
        isNight && !def->second.loopNight.empty() ? def->second.loopNight : def->second.loop;
    if (loop.empty())
    {
        return std::nullopt;
    }
    const auto id = playSound(loop);
    if (id)
    {
        m_audio->setMuffle(*id, muffle);
        m_audio->setVolume(*id, 0.0f);
        m_audio->setVolume(*id, volume, def->second.fade); // faded in
    }
    return id;
}

void Engine::updateAmbience(f32 seconds)
{
    // The ambience of the zone the hero stands in (or the camera without one); by day or by night. In a room
    // (an indoor zone) the ambience "innen" with the outside one muffled behind it - unless an ambient zone
    // smaller than the room says otherwise (a smithy's forge).
    const Vec3 at = m_player.valid() ? m_playerFeet + Vec3(0.0f, 1.0f, 0.0f) : m_camera.transform.position;
    f32 ambientVolume = 0.0f;
    f32 roomVolume = 0.0f;
    const world::Zone* ambient = smallestZoneAt("ambient", at, &ambientVolume);
    const world::Zone* room = smallestZoneAt("indoor", at, &roomVolume);
    std::string name = ambient != nullptr ? ambient->value : std::string();
    std::string outside;
    if (room != nullptr && (ambient == nullptr || ambientVolume >= roomVolume) &&
        m_ambientDefs.contains(kIndoorAmbience))
    {
        outside = name;
        name = kIndoorAmbience;
    }
    const bool isNight = night();
    const auto def = m_ambientDefs.find(name);
    if (outside != m_ambience.outside || isNight != m_ambience.night)
    {
        if (m_ambience.outsideLoop)
        {
            m_audio->stop(*m_ambience.outsideLoop, 1.0f);
            m_ambience.outsideLoop.reset();
        }
        m_ambience.outside = outside;
        if (!outside.empty())
        {
            m_ambience.outsideLoop = startAmbienceLoop(outside, isNight, kOutsideVolume, kOutsideMuffle);
        }
    }
    if (name != m_ambience.name || isNight != m_ambience.night)
    {
        const f32 fade = def != m_ambientDefs.end() ? def->second.fade : 2.0f;
        if (m_ambience.loop)
        {
            m_audio->stop(*m_ambience.loop, fade); // the old one fades out while the new one fades in
            m_ambience.loop.reset();
        }
        m_ambience.name = name;
        m_ambience.night = isNight;
        if (def != m_ambientDefs.end())
        {
            m_ambience.loop = startAmbienceLoop(name, isNight, 1.0f, 0.0f);
            m_ambience.nextRandom =
                std::uniform_real_distribution<f32>(def->second.intervalMin, def->second.intervalMax)(m_rng);
        }
        G7_LOG_DEBUG("engine", "ambience: '{}' ({}){}", name, isNight ? "night" : "day",
                     outside.empty() ? std::string() : std::format(", outside '{}' muffled", outside));
    }
    if (def == m_ambientDefs.end())
    {
        return;
    }
    // Single sounds now and then, somewhere around the listener.
    const audio::AmbientDef& d = def->second;
    const auto& randoms = isNight && !d.randomsNight.empty() ? d.randomsNight : d.randoms;
    m_ambience.nextRandom -= seconds;
    if (randoms.empty() || m_ambience.nextRandom > 0.0f)
    {
        return;
    }
    std::uniform_real_distribution<f32> unit(0.0f, 1.0f);
    const f32 angle = unit(m_rng) * glm::two_pi<f32>();
    const f32 distance = d.distanceMin + unit(m_rng) * (d.distanceMax - d.distanceMin);
    const Vec3 where =
        m_camera.transform.position +
        Vec3(std::cos(angle) * distance, 1.0f + unit(m_rng) * 4.0f, std::sin(angle) * distance);
    const auto pick = static_cast<usize>(unit(m_rng) * static_cast<f32>(randoms.size()));
    (void)playSound(randoms[std::min(pick, randoms.size() - 1)], where);
    m_ambience.nextRandom = d.intervalMin + unit(m_rng) * (d.intervalMax - d.intervalMin);
}

void Engine::updateOcclusion(f32 seconds)
{
    // Owner decision 8: a wall between the listener and a 3D sound muffles it (a low-pass), a few times a
    // second.
    m_occlusionTimer -= seconds;
    if (m_occlusionTimer > 0.0f)
    {
        return;
    }
    m_occlusionTimer = kOcclusionInterval;
    const Vec3 ear = m_camera.transform.position;
    for (auto it = m_spatialSounds.begin(); it != m_spatialSounds.end();)
    {
        if (!m_audio->playing(it->first))
        {
            it = m_spatialSounds.erase(it);
            continue;
        }
        const Vec3 to = it->second - ear;
        const f32 length = glm::length(to);
        bool blocked = false;
        if (m_physics.valid() && length > 0.5f)
        {
            const auto hit =
                m_physics.raycast(ear, to / length, length - 0.3f, physics::layerBit(physics::Layer::World));
            blocked = hit.has_value();
        }
        m_audio->setMuffle(it->first, blocked ? kOccludedMuffle : 0.0f);
        ++it;
    }
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
    if (position)
    {
        m_spatialSounds[played.value()] = *position; // occlusion (part B)
    }
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

// The mixer (M13 part A, ADR 0007): miniaudio's engine with one sound group per bus; clips are decoded once
// to float PCM at the mixer's rate, every playing sound reads them through its own buffer reference.

#include <g7/audio/Audio.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>

#define STB_VORBIS_HEADER_ONLY
#include <miniaudio.h>
#include <stb_vorbis.c>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <random>
#include <unordered_map>

namespace g7::audio
{
namespace
{
constexpr std::array<std::string_view, static_cast<usize>(Bus::Count)> kBusNames = {"music", "effects",
                                                                                    "voice", "ambient", "ui"};

struct Clip
{
    std::vector<f32> samples; ///< interleaved
    u32 channels = 0;
    u64 frames = 0;
};

struct Voice
{
    std::unique_ptr<ma_audio_buffer_ref> source;
    std::unique_ptr<ma_sound> sound;
    std::shared_ptr<const Clip> clip; ///< kept while it plays
    u64 startFrame = 0;               ///< on the mixer's clock: before it, a delayed sound waits
};
} // namespace

std::string_view moduleName() noexcept
{
    return "audio";
}

std::string_view busName(Bus bus) noexcept
{
    const auto i = static_cast<usize>(bus);
    return i < kBusNames.size() ? kBusNames[i] : std::string_view("?");
}

std::optional<Bus> busFromName(std::string_view name) noexcept
{
    for (usize i = 0; i < kBusNames.size(); ++i)
    {
        if (kBusNames[i] == name)
        {
            return static_cast<Bus>(i);
        }
    }
    return std::nullopt;
}

Result<SoundDefs> parseSoundDefs(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    SoundDefs defs;
    for (const std::string& name : c.keys())
    {
        SoundDef d;
        d.files = c.get<std::vector<std::string>>(name + ".files", {});
        if (d.files.empty())
        {
            return Error{std::format("{}: sound '{}' has no files", source, name)};
        }
        d.volume = static_cast<f32>(c.get<f64>(name + ".volume", 1.0));
        d.volumeJitter = static_cast<f32>(c.get<f64>(name + ".volume_jitter", 0.0));
        d.pitchJitter = static_cast<f32>(c.get<f64>(name + ".pitch_jitter", 0.0));
        d.minDistance = static_cast<f32>(c.get<f64>(name + ".min_distance", 1.0));
        d.maxDistance = static_cast<f32>(c.get<f64>(name + ".max_distance", 30.0));
        d.loop = c.get<bool>(name + ".loop", false);
        const std::string bus = c.get<std::string>(name + ".bus", "effects");
        const auto b = busFromName(bus);
        if (!b)
        {
            return Error{std::format("{}: sound '{}': unknown bus '{}' (music, effects, voice, ambient, ui)",
                                     source, name, bus)};
        }
        d.bus = *b;
        if (d.volume < 0.0f || d.minDistance <= 0.0f || d.maxDistance < d.minDistance ||
            d.pitchJitter < 0.0f || d.pitchJitter >= 1.0f || d.volumeJitter < 0.0f || d.volumeJitter > 1.0f)
        {
            return Error{
                std::format("{}: sound '{}': volume >= 0, 0 < min_distance <= max_distance, jitter in "
                            "[0, 1)",
                            source, name)};
        }
        defs.emplace(name, std::move(d));
    }
    return defs;
}

struct AudioSystem::Impl
{
    AudioConfig config;
    ma_engine engine{};
    bool engineReady = false;
    std::array<ma_sound_group, static_cast<usize>(Bus::Count)> groups{};
    std::array<f32, static_cast<usize>(Bus::Count)> busVolumes{};
    usize groupsReady = 0;
    std::unordered_map<std::string, std::shared_ptr<const Clip>> clips;
    std::unordered_map<SoundId, Voice> voices;
    SoundId nextId = 1;
    std::mt19937 rng;
    std::vector<f32> scratch; ///< rendering without a device
    f32 peak = 0.0f;

    ~Impl()
    {
        for (auto& [id, v] : voices)
        {
            ma_sound_uninit(v.sound.get());
            ma_audio_buffer_ref_uninit(v.source.get());
        }
        voices.clear();
        for (usize i = 0; i < groupsReady; ++i)
        {
            ma_sound_group_uninit(&groups[i]);
        }
        if (engineReady)
        {
            ma_engine_uninit(&engine);
        }
    }

    /// Waiting for its start, or playing and not at its end.
    bool alive(const Voice& v)
    {
        if (ma_engine_get_time_in_pcm_frames(&engine) < v.startFrame)
        {
            return true;
        }
        return ma_sound_is_playing(v.sound.get()) && !ma_sound_at_end(v.sound.get());
    }

    Voice* voice(SoundId id)
    {
        const auto it = voices.find(id);
        return it != voices.end() ? &it->second : nullptr;
    }
};

AudioSystem::AudioSystem(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl))
{
}
AudioSystem::AudioSystem(AudioSystem&&) noexcept = default;
AudioSystem& AudioSystem::operator=(AudioSystem&&) noexcept = default;
AudioSystem::~AudioSystem() = default;

Result<AudioSystem> AudioSystem::create(const AudioConfig& config)
{
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->rng.seed(config.seed);
    ma_engine_config ec = ma_engine_config_init();
    ec.channels = 2;
    ec.sampleRate = config.sampleRate;
    ec.listenerCount = 1;
    ec.noDevice = config.device ? MA_FALSE : MA_TRUE;
    if (const ma_result r = ma_engine_init(&ec, &impl->engine); r != MA_SUCCESS)
    {
        return Error{std::format("audio: {} ({})", config.device ? "no output device" : "mixer failed",
                                 ma_result_description(r))};
    }
    impl->engineReady = true;
    for (usize i = 0; i < impl->groups.size(); ++i)
    {
        if (const ma_result r = ma_sound_group_init(&impl->engine, 0, nullptr, &impl->groups[i]);
            r != MA_SUCCESS)
        {
            return Error{std::format("audio: bus {}: {}", kBusNames[i], ma_result_description(r))};
        }
        impl->busVolumes[i] = 1.0f;
        ++impl->groupsReady;
    }
    G7_LOG_INFO("audio", "mixer {} Hz, {}", config.sampleRate,
                config.device ? "device" : "no device (rendered)");
    return AudioSystem(std::move(impl));
}

Result<void> AudioSystem::addClip(std::string_view name, std::span<const u8> bytes)
{
    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, m_impl->config.sampleRate);
    ma_decoder decoder;
    if (const ma_result r = ma_decoder_init_memory(bytes.data(), bytes.size(), &dc, &decoder);
        r != MA_SUCCESS)
    {
        return Error{std::format("audio: {}: cannot decode ({})", name, ma_result_description(r))};
    }
    auto clip = std::make_shared<Clip>();
    clip->channels = decoder.outputChannels;
    std::array<f32, 4096> block{};
    const u64 perBlock = block.size() / std::max<u32>(clip->channels, 1);
    for (;;)
    {
        ma_uint64 read = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&decoder, block.data(), perBlock, &read);
        clip->samples.insert(clip->samples.end(), block.begin(),
                             block.begin() + static_cast<std::ptrdiff_t>(read * clip->channels));
        if (r != MA_SUCCESS || read < perBlock)
        {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    clip->frames = clip->channels > 0 ? clip->samples.size() / clip->channels : 0;
    if (clip->frames == 0)
    {
        return Error{std::format("audio: {}: no samples", name)};
    }
    m_impl->clips[std::string(name)] = std::move(clip);
    return {};
}

bool AudioSystem::hasClip(std::string_view name) const noexcept
{
    return m_impl->clips.contains(std::string(name));
}

f32 AudioSystem::clipSeconds(std::string_view name) const noexcept
{
    const auto it = m_impl->clips.find(std::string(name));
    return it == m_impl->clips.end()
               ? 0.0f
               : static_cast<f32>(it->second->frames) / static_cast<f32>(m_impl->config.sampleRate);
}

Result<SoundId> AudioSystem::play(const SoundDef& def, std::optional<Vec3> position, f32 delaySeconds)
{
    if (def.files.empty())
    {
        return Error{"audio: a sound without files"};
    }
    const std::string& file =
        def.files[std::uniform_int_distribution<usize>(0, def.files.size() - 1)(m_impl->rng)];
    const auto clip = m_impl->clips.find(file);
    if (clip == m_impl->clips.end())
    {
        return Error{std::format("audio: {} is not loaded", file)};
    }
    Voice v;
    v.clip = clip->second;
    v.source = std::make_unique<ma_audio_buffer_ref>();
    v.sound = std::make_unique<ma_sound>();
    if (const ma_result r = ma_audio_buffer_ref_init(ma_format_f32, v.clip->channels, v.clip->samples.data(),
                                                     v.clip->frames, v.source.get());
        r != MA_SUCCESS)
    {
        return Error{std::format("audio: {}: {}", file, ma_result_description(r))};
    }
    const ma_uint32 flags = position ? 0u : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_SPATIALIZATION);
    if (const ma_result r =
            ma_sound_init_from_data_source(&m_impl->engine, v.source.get(), flags,
                                           &m_impl->groups[static_cast<usize>(def.bus)], v.sound.get());
        r != MA_SUCCESS)
    {
        ma_audio_buffer_ref_uninit(v.source.get());
        return Error{std::format("audio: {}: {}", file, ma_result_description(r))};
    }
    std::uniform_real_distribution<f32> spread(-1.0f, 1.0f);
    ma_sound_set_volume(v.sound.get(),
                        std::max(0.0f, def.volume * (1.0f + def.volumeJitter * spread(m_impl->rng))));
    ma_sound_set_pitch(v.sound.get(), 1.0f + def.pitchJitter * spread(m_impl->rng));
    ma_sound_set_looping(v.sound.get(), def.loop ? MA_TRUE : MA_FALSE);
    if (position)
    {
        ma_sound_set_position(v.sound.get(), position->x, position->y, position->z);
        ma_sound_set_attenuation_model(v.sound.get(), ma_attenuation_model_linear);
        ma_sound_set_min_distance(v.sound.get(), def.minDistance);
        ma_sound_set_max_distance(v.sound.get(), def.maxDistance);
        ma_sound_set_doppler_factor(v.sound.get(), 0.0f); // owner decision: no Doppler
    }
    if (delaySeconds > 0.0f)
    {
        const ma_uint64 at = ma_engine_get_time_in_pcm_frames(&m_impl->engine) +
                             static_cast<ma_uint64>(std::llround(delaySeconds * m_impl->config.sampleRate));
        ma_sound_set_start_time_in_pcm_frames(v.sound.get(), at);
        v.startFrame = at;
    }
    ma_sound_start(v.sound.get());
    const SoundId id = m_impl->nextId++;
    m_impl->voices.emplace(id, std::move(v));
    return id;
}

void AudioSystem::stop(SoundId id, f32 fadeSeconds)
{
    Voice* v = m_impl->voice(id);
    if (v == nullptr)
    {
        return;
    }
    if (fadeSeconds > 0.0f)
    {
        ma_sound_stop_with_fade_in_pcm_frames(
            v->sound.get(), static_cast<ma_uint64>(std::llround(fadeSeconds * m_impl->config.sampleRate)));
    }
    else
    {
        ma_sound_stop(v->sound.get());
    }
}

bool AudioSystem::playing(SoundId id) const noexcept
{
    const auto it = m_impl->voices.find(id);
    return it != m_impl->voices.end() && m_impl->alive(it->second);
}

void AudioSystem::setPosition(SoundId id, const Vec3& position)
{
    if (Voice* v = m_impl->voice(id))
    {
        ma_sound_set_position(v->sound.get(), position.x, position.y, position.z);
    }
}

void AudioSystem::setVolume(SoundId id, f32 volume, f32 fadeSeconds)
{
    if (Voice* v = m_impl->voice(id))
    {
        ma_sound_set_fade_in_pcm_frames(
            v->sound.get(), -1.0f, volume,
            static_cast<ma_uint64>(std::llround(std::max(fadeSeconds, 0.0f) * m_impl->config.sampleRate)));
    }
}

void AudioSystem::setListener(const Vec3& position, const Vec3& forward, const Vec3& up)
{
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
}

void AudioSystem::setMasterVolume(f32 volume)
{
    ma_engine_set_volume(&m_impl->engine, std::max(volume, 0.0f));
}

void AudioSystem::setBusVolume(Bus bus, f32 volume)
{
    const auto i = static_cast<usize>(bus);
    if (i < m_impl->groups.size())
    {
        m_impl->busVolumes[i] = std::max(volume, 0.0f);
        ma_sound_group_set_volume(&m_impl->groups[i], m_impl->busVolumes[i]);
    }
}

f32 AudioSystem::busVolume(Bus bus) const noexcept
{
    const auto i = static_cast<usize>(bus);
    return i < m_impl->busVolumes.size() ? m_impl->busVolumes[i] : 0.0f;
}

f64 AudioSystem::time() const noexcept
{
    return static_cast<f64>(ma_engine_get_time_in_pcm_frames(&m_impl->engine)) /
           static_cast<f64>(m_impl->config.sampleRate);
}

void AudioSystem::update(f32 seconds)
{
    if (!m_impl->config.device && seconds > 0.0f)
    {
        // No device: the caller's clock drives the mixer.
        m_impl->peak = 0.0f;
        ma_uint64 left = static_cast<ma_uint64>(std::llround(seconds * m_impl->config.sampleRate));
        constexpr ma_uint64 kBlock = 1024;
        m_impl->scratch.resize(kBlock * 2);
        while (left > 0)
        {
            const ma_uint64 frames = std::min(left, kBlock);
            ma_uint64 read = 0;
            ma_engine_read_pcm_frames(&m_impl->engine, m_impl->scratch.data(), frames, &read);
            for (ma_uint64 i = 0; i < read * 2; ++i)
            {
                m_impl->peak = std::max(m_impl->peak, std::abs(m_impl->scratch[i]));
            }
            left -= frames;
        }
    }
    // Free what ended (or was stopped and faded out).
    for (auto it = m_impl->voices.begin(); it != m_impl->voices.end();)
    {
        ma_sound* s = it->second.sound.get();
        if (!m_impl->alive(it->second))
        {
            ma_sound_uninit(s);
            ma_audio_buffer_ref_uninit(it->second.source.get());
            it = m_impl->voices.erase(it);
            continue;
        }
        ++it;
    }
}

usize AudioSystem::playingCount() const noexcept
{
    return m_impl->voices.size();
}

f32 AudioSystem::lastPeak() const noexcept
{
    return m_impl->peak;
}
} // namespace g7::audio

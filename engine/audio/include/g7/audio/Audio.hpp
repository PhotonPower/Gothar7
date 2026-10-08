#pragma once

// Module: g7::audio (M13, ADR 0007) - sounds and their mixer: clips decoded from bytes (WAV, FLAC, MP3, OGG
// Vorbis), one-shots and looping sounds, 3D placement against a listener, buses with volumes. miniaudio stays
// private (PImpl). Without an audio device (headless, CI) the caller drives the clock: update() renders the
// frames itself, so tests are deterministic.
// Specification: docs/modules/audio.md

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::audio
{
/// Returns the module name (used for diagnostics and the module registry).
[[nodiscard]] std::string_view moduleName() noexcept;

/// The mixer's groups under the master (docs/modules/audio.md).
enum class Bus : u8
{
    Music,
    Effects,
    Voice,
    Ambient,
    Ui,
    Count,
};
[[nodiscard]] std::string_view busName(Bus bus) noexcept;
[[nodiscard]] std::optional<Bus> busFromName(std::string_view name) noexcept;

/// A sound as data (data/sounds.toml): one of its files at random, volume and pitch spread, distances.
struct SoundDef
{
    std::vector<std::string> files; ///< VFS paths (sounds/anvil_hit.wav ...)
    f32 volume = 1.0f;
    f32 volumeJitter = 0.0f; ///< ± fraction of the volume
    f32 pitchJitter = 0.0f;  ///< ± fraction of the pitch
    f32 minDistance = 1.0f;  ///< m: full volume up to here (3D)
    f32 maxDistance = 30.0f; ///< m: silent from here (3D)
    Bus bus = Bus::Effects;
    bool loop = false;
};
using SoundDefs = std::map<std::string, SoundDef, std::less<>>;

/// `[name] files = [...] volume = ... bus = "effects" ...` - all tables of the file; errors name the sound.
[[nodiscard]] Result<SoundDefs> parseSoundDefs(std::string_view toml, std::string_view source);

/// An ambience (data/ambient.toml, M13 part B): a loop and single sounds now and then around the listener, by
/// day and by night; the names are sounds of data/sounds.toml.
struct AmbientDef
{
    std::string loop;      ///< by day (and by night without loop_night); empty: none
    std::string loopNight; ///< empty: `loop`
    std::vector<std::string> randoms;
    std::vector<std::string> randomsNight; ///< empty: `randoms`
    f32 intervalMin = 8.0f;                ///< s between single sounds
    f32 intervalMax = 20.0f;
    f32 distanceMin = 4.0f; ///< m from the listener
    f32 distanceMax = 15.0f;
    f32 fade = 2.0f; ///< s cross-fade when the ambience changes
};
using AmbientDefs = std::map<std::string, AmbientDef, std::less<>>;
[[nodiscard]] Result<AmbientDefs> parseAmbientDefs(std::string_view toml, std::string_view source);

struct AudioConfig
{
    bool device = true;     ///< false: no output device, update() renders (headless, tests)
    u32 sampleRate = 48000; ///< of the mixer
    u32 seed = 0;           ///< random choice of files and jitter
};

using SoundId = u32;

class AudioSystem
{
public:
    /// Fails if a device was asked for and none can be opened (the caller may retry without).
    [[nodiscard]] static Result<AudioSystem> create(const AudioConfig& config);
    AudioSystem(AudioSystem&&) noexcept;
    AudioSystem& operator=(AudioSystem&&) noexcept;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    ~AudioSystem();

    /// Decodes a clip (WAV, FLAC, MP3, OGG Vorbis) and keeps it under `name` (its VFS path).
    Result<void> addClip(std::string_view name, std::span<const u8> bytes);
    [[nodiscard]] bool hasClip(std::string_view name) const noexcept;
    [[nodiscard]] f32 clipSeconds(std::string_view name) const noexcept; ///< 0: unknown
    [[nodiscard]] u64 clipFrames(std::string_view name) const noexcept;  ///< at the mixer's rate; 0: unknown
    [[nodiscard]] u32 sampleRate() const noexcept;

    /// Plays one of the definition's files (all must have been added); at `position` it is a 3D sound.
    /// `delaySeconds` starts it later on the mixer's clock (sample exact: music on bar boundaries).
    Result<SoundId> play(const SoundDef& def, std::optional<Vec3> position = {}, f32 delaySeconds = 0.0f);
    /// A 2D sound starting at `frame` of the mixer's clock (music: segments chained sample exact, M13 C).
    Result<SoundId> playAtFrame(const SoundDef& def, u64 frame);
    /// Stops a sound, fading out over `fadeSeconds`. A sound still waiting for its start never sounds.
    void stop(SoundId id, f32 fadeSeconds = 0.0f);
    /// Stops it at `frame` of the mixer's clock, the fade ending there (music on a bar boundary).
    void stopAtFrame(SoundId id, u64 frame, f32 fadeSeconds = 0.0f);
    [[nodiscard]] bool playing(SoundId id) const noexcept;
    void setPosition(SoundId id, const Vec3& position);
    /// A sound's own volume (on top of its definition's), faded over `fadeSeconds`.
    void setVolume(SoundId id, f32 volume, f32 fadeSeconds = 0.0f);
    /// A low-pass cut, 0 (open) .. 1 (strongly muffled) - occlusion (owner decision 8), the outside from a
    /// room.
    void setMuffle(SoundId id, f32 amount);
    [[nodiscard]] f32 muffle(SoundId id) const noexcept;

    void setListener(const Vec3& position, const Vec3& forward, const Vec3& up = Vec3(0.0f, 1.0f, 0.0f));
    void setMasterVolume(f32 volume);
    void setBusVolume(Bus bus, f32 volume);
    [[nodiscard]] f32 busVolume(Bus bus) const noexcept;

    /// The mixer's clock in seconds (frames rendered or played).
    [[nodiscard]] f64 time() const noexcept;
    [[nodiscard]] u64 frame() const noexcept; ///< the same in frames
    /// Without a device: renders `seconds` of audio. Always: frees the sounds that ended.
    void update(f32 seconds);
    [[nodiscard]] usize playingCount() const noexcept;
    /// Without a device: the loudest sample of the frames rendered by the last update (tests, meters).
    [[nodiscard]] f32 lastPeak() const noexcept;

private:
    struct Impl;
    explicit AudioSystem(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::audio

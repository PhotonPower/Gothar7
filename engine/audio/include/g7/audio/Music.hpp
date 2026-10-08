#pragma once

// Dynamic music (M13 part C, owner decisions 4 and 5; Gothic: DirectMusic): the music zone gives a theme, the
// game a state (standard, threat, fight), the clock day or night; each combination has a set of segments in
// data/music.toml. Segments follow one another sample exact; a change starts on the running segment's next
// bar (or at its end), the old one fading out up to there. Stingers sound over the music on the next beat.
// Specification: docs/modules/audio.md

#include <g7/audio/Audio.hpp>

#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace g7::audio
{
enum class MusicState : u8
{
    Std, ///< standard
    Thr, ///< threat: an enemy sees the hero and threatens or hunts him
    Fgt, ///< fight
};
[[nodiscard]] std::string_view musicStateName(MusicState state) noexcept; ///< "std", "thr", "fgt"
[[nodiscard]] std::optional<MusicState> musicStateFromName(std::string_view name) noexcept;

enum class MusicTransition : u8
{
    NextBar, ///< on the running segment's next bar boundary
    End,     ///< when the running segment ends
};

/// The segments of one theme × state (× day/night).
struct MusicSet
{
    std::vector<std::string> segments; ///< VFS paths, one at random after the other (not the same twice)
    std::string intro;                 ///< optional: played once first when the music changes to this set
    MusicTransition transition = MusicTransition::NextBar;
    f32 fade = 0.3f; ///< s: the old segment fades out up to the change
    f32 bpm = 0.0f;  ///< its own tempo (common's threat and fight differ); 0: the theme's
    u32 beats = 0;   ///< per bar; 0: the theme's
};

struct MusicTheme
{
    f32 bpm = 100.0f;
    u32 beats = 4; ///< per bar
    f32 volume = 1.0f;
    /// "std", "thr", "fgt" or with the time of day "std.day", "std.ngt" ...
    std::map<std::string, MusicSet, std::less<>> sets;
    [[nodiscard]] f64 barSeconds() const noexcept { return 60.0 * beats / bpm; }
};

struct MusicStinger
{
    std::vector<std::string> files;
    f32 volume = 1.0f;
};

/// data/music.toml. The theme "common" is no zone's: its sets serve every theme without its own (shared
/// threat and fight music).
struct MusicDefs
{
    std::map<std::string, MusicTheme, std::less<>> themes;
    std::map<std::string, MusicStinger, std::less<>> stingers;
    std::vector<std::string> threatStates; ///< NPC states (zs_...) that make a threat
    std::vector<std::string> fightStates;  ///< ... and a fight
    f32 threatRange = 25.0f;               ///< m: enemies farther away do not count
    f32 fightRange = 20.0f;
    f32 fightMemory = 4.0f; ///< s: the hero's own hits (given or taken) keep the fight going
    f32 hysteresis = 5.0f;  ///< s without cause before the state goes down (owner decision 4)
    /// States with music outside every music zone (from "common"); the others are silent there (owner: no
    /// music outside the zones).
    std::vector<MusicState> outside;
};
[[nodiscard]] Result<MusicDefs> parseMusicDefs(std::string_view toml, std::string_view source);

/// What plays for a theme, state and time of day: theme.state.time, theme.state, then the same in "common";
/// a state without music anywhere falls back to the next lower one. Outside the zones (theme empty or without
/// music) only the states of `outside`, from "common". nullopt: silence.
struct MusicChoice
{
    std::string owner; ///< the theme whose set it is ("common" ...)
    std::string key;   ///< "fgt", "std.day" ...
    const MusicTheme* theme = nullptr;
    const MusicSet* set = nullptr;
    [[nodiscard]] std::string id() const { return owner + "/" + key; }
    /// The set's bar (its own tempo, else its theme's), in seconds.
    [[nodiscard]] f64 barSeconds() const noexcept;
    [[nodiscard]] u32 beats() const noexcept { return set->beats > 0 ? set->beats : theme->beats; }
};
[[nodiscard]] std::optional<MusicChoice> chooseMusic(const MusicDefs& defs, std::string_view theme,
                                                     MusicState state, bool night);

/// The state with hysteresis: up at once, down only after `hold` seconds without the higher cause.
class MusicStateFilter
{
public:
    MusicState update(MusicState raw, f32 seconds, f32 hold) noexcept;
    [[nodiscard]] MusicState state() const noexcept { return m_state; }
    void reset() noexcept { *this = {}; }

private:
    MusicState m_state = MusicState::Std;
    f32 m_calm = 0.0f; ///< s the raw state has been below m_state
};

class MusicPlayer
{
public:
    explicit MusicPlayer(MusicDefs defs = {}, u32 seed = 0);

    [[nodiscard]] const MusicDefs& defs() const noexcept { return m_defs; }
    /// Every file the definitions name (to load them into the AudioSystem).
    [[nodiscard]] std::vector<std::string> files() const;

    /// Call often (each frame): plays what fits, plans changes on bar boundaries. `theme` empty: silence.
    void update(AudioSystem& audio, std::string_view theme, MusicState state, bool night);
    /// A stinger over the music on its next beat (at once without music).
    Result<void> stinger(AudioSystem& audio, std::string_view name);
    /// Everything off (world change), fading out.
    void stop(AudioSystem& audio, f32 fadeSeconds = 0.5f);

    struct Status
    {
        std::string theme; ///< asked for
        MusicState state = MusicState::Std;
        bool night = false;
        std::string set;      ///< owner/key of what plays or comes, empty: silence
        std::string segment;  ///< the file playing (or waiting for its start)
        u64 segmentStart = 0; ///< frame
        u64 changeAt = 0;     ///< frame of the planned change, 0: none
        std::string stinger;  ///< the last stinger played
    };
    [[nodiscard]] const Status& status() const noexcept { return m_status; }

private:
    struct Segment
    {
        SoundId id = 0;
        std::string file;
        std::string set; ///< MusicChoice::id()
        u64 start = 0;
        u64 end = 0;
        u64 bar = 0;  ///< frames per bar of its theme
        u64 beat = 0; ///< frames per beat
        bool intro = false;
    };
    std::optional<Segment> start(AudioSystem& audio, const MusicChoice& choice, u64 at, bool intro);
    void queueNext(AudioSystem& audio, const MusicChoice& choice);

    MusicDefs m_defs;
    std::mt19937 m_rng;
    std::optional<Segment> m_current;   ///< playing, or waiting for a change on a bar boundary
    std::optional<Segment> m_next;      ///< the segment queued after it
    u64 m_fadingUntil = 0;              ///< the last segment fades out to silence up to this frame
    std::vector<std::string> m_missing; ///< files warned about (not loaded)
    Status m_status;
};
} // namespace g7::audio

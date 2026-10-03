#pragma once

// A clip bound to a skeleton (M6 part B): sampling, events, the root track for root motion.

#include <g7/animation/Skeleton.hpp>
#include <g7/asset/SkinnedModel.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::animation
{
/// Called for every event a playback range passes (clip name, event name).
using EventCallback = std::function<void(std::string_view clip, std::string_view event)>;

class Clip
{
public:
    Clip() = default;
    /// Tracks of bones the skeleton does not have are dropped (counted in droppedTracks()).
    Clip(const asset::ClipData& data, const Skeleton& skeleton);

    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] f32 duration() const noexcept { return m_duration; }
    [[nodiscard]] bool loops() const noexcept { return m_loops; }
    [[nodiscard]] usize droppedTracks() const noexcept { return m_dropped; }
    [[nodiscard]] const std::vector<asset::ClipEvent>& events() const noexcept { return m_events; }

    /// Writes the tracked channels at `time` (seconds; loops wrap, others clamp) into `pose`; untracked
    /// bones keep what `pose` had (start from the rest pose).
    void sample(f32 time, Pose& pose) const;
    /// Translation of the root bone at `time` (zero without a root track): root motion.
    [[nodiscard]] Vec3 rootTranslation(f32 time) const;
    /// Fires the events in (from, to], in order; a loop that wraps fires the rest of the cycle first.
    /// `to - from` may span several cycles of a loop (each event fires once per cycle passed).
    void fireEvents(f32 from, f32 to, const EventCallback& callback) const;

private:
    struct Track
    {
        u32 bone = 0;
        asset::TrackData::Path path = asset::TrackData::Path::Rotation;
        bool step = false;
        std::vector<f32> times;
        std::vector<Vec4> values;
    };
    [[nodiscard]] f32 wrap(f32 time) const noexcept;
    [[nodiscard]] static Vec4 valueAt(const Track& track, f32 time);

    std::string m_name;
    f32 m_duration = 0.0f;
    bool m_loops = false;
    std::vector<Track> m_tracks;
    i32 m_rootTrack = -1; ///< translation track of the bone "root"
    std::vector<asset::ClipEvent> m_events;
    usize m_dropped = 0;
};
} // namespace g7::animation

#include <g7/animation/Clip.hpp>

#include <algorithm>
#include <cmath>

namespace g7::animation
{
Clip::Clip(const asset::ClipData& data, const Skeleton& skeleton)
    : m_name(data.name), m_duration(data.duration), m_loops(data.loops()), m_events(data.events)
{
    for (const asset::TrackData& t : data.tracks)
    {
        const i32 bone = skeleton.find(t.bone);
        if (bone < 0 || t.times.empty())
        {
            ++m_dropped;
            continue;
        }
        if (t.bone == "root" && t.path == asset::TrackData::Path::Translation)
        {
            m_rootTrack = static_cast<i32>(m_tracks.size());
        }
        m_tracks.push_back({static_cast<u32>(bone), t.path, t.step, t.times, t.values});
    }
}

f32 Clip::wrap(f32 time) const noexcept
{
    if (m_duration <= 0.0f)
    {
        return 0.0f;
    }
    if (m_loops)
    {
        const f32 t = std::fmod(time, m_duration);
        return t < 0.0f ? t + m_duration : t;
    }
    return std::clamp(time, 0.0f, m_duration);
}

Vec4 Clip::valueAt(const Track& track, f32 time)
{
    const auto& times = track.times;
    if (time <= times.front())
    {
        return track.values.front();
    }
    if (time >= times.back())
    {
        return track.values.back();
    }
    const usize next = static_cast<usize>(std::upper_bound(times.begin(), times.end(), time) - times.begin());
    const usize prev = next - 1;
    if (track.step)
    {
        return track.values[prev];
    }
    const f32 span = times[next] - times[prev];
    const f32 t = span > 0.0f ? (time - times[prev]) / span : 0.0f;
    const Vec4& a = track.values[prev];
    const Vec4& b = track.values[next];
    if (track.path == asset::TrackData::Path::Rotation)
    {
        // Quaternions stored xyzw: slerp on the shortest arc.
        const Quat qa(a.w, a.x, a.y, a.z);
        Quat qb(b.w, b.x, b.y, b.z);
        const Quat q = glm::slerp(qa, glm::dot(qa, qb) < 0.0f ? -qb : qb, t);
        return Vec4(q.x, q.y, q.z, q.w);
    }
    return glm::mix(a, b, t);
}

void Clip::sample(f32 time, Pose& pose) const
{
    const f32 t = wrap(time);
    for (const Track& track : m_tracks)
    {
        const Vec4 v = valueAt(track, t);
        BoneTransform& bone = pose[track.bone];
        switch (track.path)
        {
        case asset::TrackData::Path::Translation:
            bone.translation = Vec3(v);
            break;
        case asset::TrackData::Path::Rotation:
            bone.rotation = glm::normalize(Quat(v.w, v.x, v.y, v.z));
            break;
        case asset::TrackData::Path::Scale:
            bone.scale = Vec3(v);
            break;
        }
    }
}

Vec3 Clip::rootTranslation(f32 time) const
{
    return m_rootTrack < 0 ? Vec3(0.0f)
                           : Vec3(valueAt(m_tracks[static_cast<usize>(m_rootTrack)], wrap(time)));
}

void Clip::fireEvents(f32 from, f32 to, const EventCallback& callback) const
{
    if (!callback || m_events.empty() || to <= from)
    {
        return;
    }
    if (!m_loops || m_duration <= 0.0f)
    {
        for (const asset::ClipEvent& e : m_events)
        {
            if (e.time > from && e.time <= to)
            {
                callback(m_name, e.name);
            }
        }
        return;
    }
    // Loops: walk cycle by cycle from the cycle `from` lies in.
    f32 cycleStart = std::floor(from / m_duration) * m_duration;
    for (int guard = 0; cycleStart < to && guard < 1000; ++guard, cycleStart += m_duration)
    {
        for (const asset::ClipEvent& e : m_events)
        {
            const f32 at = cycleStart + e.time;
            if (at > from && at <= to)
            {
                callback(m_name, e.name);
            }
        }
    }
}
} // namespace g7::animation

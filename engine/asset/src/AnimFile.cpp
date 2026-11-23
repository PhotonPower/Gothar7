#include "ByteIo.hpp"

#include <g7/asset/AnimFile.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace g7::asset
{
namespace
{
using detail::ByteReader;
using detail::ByteWriter;

constexpr u64 kMinBoneSize = 2 + 4 + 10 * 4;
constexpr u64 kMinClipSize = 2 + 4 + 4;
constexpr u64 kMinTrackSize = 2 + 1 + 1 + 4;
constexpr u64 kKeySize = 5 * 4;

std::string_view clamp16(const std::string& s)
{
    return std::string_view(s).substr(0, std::numeric_limits<u16>::max());
}

Error animError(std::string_view debugName, const std::string& what)
{
    return Error{std::string(debugName) + ": corrupt .g7anim (" + what + ")"};
}

void writeFloats(ByteWriter& w, const f32* v, int n)
{
    for (int i = 0; i < n; ++i)
    {
        w.f32v(v[i]);
    }
}

void readFloats(ByteReader& r, f32* v, int n)
{
    for (int i = 0; i < n; ++i)
    {
        v[i] = r.f32v();
    }
}

bool fits(const ByteReader& r, u64 count, u64 minSize)
{
    return r.has(count * minSize);
}
} // namespace

std::vector<u8> serializeAnim(const AnimationSetData& set)
{
    std::vector<u8> out;
    ByteWriter w(out);
    w.text(std::string_view(kAnimMagic, sizeof(kAnimMagic)));
    w.u32v(kAnimVersion);

    const SkeletonData& s = set.skeleton;
    w.u32v(static_cast<u32>(s.size()));
    for (usize b = 0; b < s.size(); ++b)
    {
        w.string16(clamp16(s.names[b]));
        w.i32v(s.parents[b]);
        writeFloats(w, &s.translations[b].x, 3);
        const Quat& q = s.rotations[b];
        w.f32v(q.x);
        w.f32v(q.y);
        w.f32v(q.z);
        w.f32v(q.w);
        writeFloats(w, &s.scales[b].x, 3);
    }
    for (int c = 0; c < 4; ++c)
    {
        writeFloats(w, &s.rootParent[c].x, 4);
    }

    w.u32v(static_cast<u32>(set.clips.size()));
    for (const ClipData& clip : set.clips)
    {
        w.string16(clamp16(clip.name));
        w.f32v(clip.duration);
        w.u32v(static_cast<u32>(clip.tracks.size()));
        for (const TrackData& track : clip.tracks)
        {
            w.string16(clamp16(track.bone));
            w.u8v(static_cast<u8>(track.path));
            w.u8v(track.step ? 1 : 0);
            const usize keys = std::min(track.times.size(), track.values.size());
            w.u32v(static_cast<u32>(keys));
            for (usize k = 0; k < keys; ++k)
            {
                w.f32v(track.times[k]);
            }
            for (usize k = 0; k < keys; ++k)
            {
                writeFloats(w, &track.values[k].x, 4);
            }
        }
    }
    return out;
}

Result<AnimationSetData> deserializeAnim(std::span<const u8> bytes, std::string_view debugName)
{
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kAnimMagic, sizeof(kAnimMagic)) != 0)
    {
        return Error{std::string(debugName) + ": not a .g7anim file"};
    }
    ByteReader r(bytes);
    static_cast<void>(r.bytes(4));
    const u32 version = r.u32v();
    if (version != kAnimVersion)
    {
        return Error{std::string(debugName) + ": .g7anim version " + std::to_string(version) + ", expected " +
                     std::to_string(kAnimVersion) + " (cook again)"};
    }
    AnimationSetData set;
    const u32 boneCount = r.u32v();
    if (boneCount > kMaxBones || !fits(r, boneCount, kMinBoneSize))
    {
        return animError(debugName, "bone count");
    }
    SkeletonData& s = set.skeleton;
    s.names.resize(boneCount);
    s.parents.resize(boneCount);
    s.translations.resize(boneCount);
    s.rotations.resize(boneCount);
    s.scales.resize(boneCount);
    for (u32 b = 0; b < boneCount; ++b)
    {
        s.names[b] = r.string16();
        s.parents[b] = r.i32v();
        readFloats(r, &s.translations[b].x, 3);
        Quat& q = s.rotations[b];
        q.x = r.f32v();
        q.y = r.f32v();
        q.z = r.f32v();
        q.w = r.f32v();
        readFloats(r, &s.scales[b].x, 3);
        if (s.parents[b] < -1 || s.parents[b] >= static_cast<i32>(b))
        {
            return animError(debugName, "bone parent after its child");
        }
    }
    for (int c = 0; c < 4; ++c)
    {
        readFloats(r, &s.rootParent[c].x, 4);
    }

    const u32 clipCount = r.u32v();
    if (!fits(r, clipCount, kMinClipSize))
    {
        return animError(debugName, "clip count");
    }
    set.clips.resize(clipCount);
    for (ClipData& clip : set.clips)
    {
        clip.name = r.string16();
        clip.duration = r.f32v();
        const u32 trackCount = r.u32v();
        if (!(clip.duration >= 0.0f) || !std::isfinite(clip.duration) || !fits(r, trackCount, kMinTrackSize))
        {
            return animError(debugName, "clip");
        }
        clip.tracks.resize(trackCount);
        for (TrackData& track : clip.tracks)
        {
            track.bone = r.string16();
            const u8 path = r.u8v();
            track.step = r.u8v() != 0;
            const u32 keys = r.u32v();
            if (path > static_cast<u8>(TrackData::Path::Scale) || !fits(r, keys, kKeySize))
            {
                return animError(debugName, "track");
            }
            track.path = static_cast<TrackData::Path>(path);
            track.times.resize(keys);
            for (u32 k = 0; k < keys; ++k)
            {
                track.times[k] = r.f32v();
                if (!std::isfinite(track.times[k]) || (k > 0 && track.times[k] < track.times[k - 1]))
                {
                    return animError(debugName, "key times not rising");
                }
            }
            track.values.resize(keys);
            for (Vec4& v : track.values)
            {
                readFloats(r, &v.x, 4);
            }
        }
        if (r.failed())
        {
            return animError(debugName, "truncated");
        }
    }
    if (r.failed())
    {
        return animError(debugName, "truncated");
    }
    if (r.remaining() != 0)
    {
        return animError(debugName, "trailing data");
    }
    return set;
}
} // namespace g7::asset

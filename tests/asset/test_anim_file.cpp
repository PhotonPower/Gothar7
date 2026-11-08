// Cooked clip sets, ".g7anim" (ADR 0025, plan M17 cooked figures part C): round trips of real sets, the
// events of events.toml applied after loading as with glTF, and corrupt files refused without crashing.

#include <g7/asset/AnimFile.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
const std::filesystem::path kCharacters = std::filesystem::path(G7_ASSET_SOURCE_DIR) / "characters";

AnimationSetData loadSet(const std::string& relative)
{
    const auto bytes = fs::readFile(kCharacters / relative);
    REQUIRE_MESSAGE(bytes.ok(), relative);
    auto set = loadAnimationGltf(bytes.value(), {}, relative);
    REQUIRE_MESSAGE(set.ok(), (set.ok() ? std::string() : set.error().message));
    return std::move(set).value();
}
} // namespace

TEST_CASE("g7anim: clip sets survive the round trip, events.toml applies to them as to glTF")
{
    for (const char* relative : {"anims/human/none.glb", "anims/human/1h.glb", "anims/human/dive.glb",
                                 "monsters/wolf/anims/wolf.glb"})
    {
        CAPTURE(relative);
        AnimationSetData source = loadSet(relative);
        const std::vector<u8> bytes = serializeAnim(source);
        auto read = deserializeAnim(bytes, relative);
        REQUIRE_MESSAGE(read.ok(), (read.ok() ? std::string() : read.error().message));
        AnimationSetData cooked = std::move(read).value();
        CHECK(serializeAnim(cooked) == bytes);
        CHECK(cooked.skeleton.names == source.skeleton.names);
        REQUIRE(cooked.clips.size() == source.clips.size());
        for (usize c = 0; c < cooked.clips.size(); ++c)
        {
            const ClipData& a = cooked.clips[c];
            const ClipData& b = source.clips[c];
            CHECK(a.name == b.name);
            CHECK(a.duration == b.duration);
            REQUIRE(a.tracks.size() == b.tracks.size());
            for (usize t = 0; t < a.tracks.size(); ++t)
            {
                CHECK(a.tracks[t].bone == b.tracks[t].bone);
                CHECK(a.tracks[t].path == b.tracks[t].path);
                CHECK(a.tracks[t].step == b.tracks[t].step);
                CHECK(a.tracks[t].times == b.tracks[t].times);
                CHECK(a.tracks[t].values == b.tracks[t].values);
            }
        }
        // The events file next to the set, if there is one, gives both the same events and speeds.
        std::filesystem::path events = kCharacters / relative;
        events.replace_extension(".events.toml");
        if (std::filesystem::exists(events))
        {
            const auto toml = fs::readText(events);
            REQUIRE(toml.ok());
            REQUIRE(applyClipEvents(source, toml.value(), "events").ok());
            REQUIRE(applyClipEvents(cooked, toml.value(), "events").ok());
            for (usize c = 0; c < cooked.clips.size(); ++c)
            {
                CHECK(cooked.clips[c].speed == source.clips[c].speed);
                REQUIRE(cooked.clips[c].events.size() == source.clips[c].events.size());
                for (usize e = 0; e < cooked.clips[c].events.size(); ++e)
                {
                    CHECK(cooked.clips[c].events[e].name == source.clips[c].events[e].name);
                    CHECK(cooked.clips[c].events[e].time == source.clips[c].events[e].time);
                }
            }
        }
    }
}

TEST_CASE("g7anim: corrupt, truncated and foreign files are refused")
{
    const std::vector<u8> bytes = serializeAnim(loadSet("anims/human/dive.glb"));
    REQUIRE(deserializeAnim(bytes).ok());
    std::vector<u8> magic = bytes;
    magic[1] = 'X';
    CHECK_FALSE(deserializeAnim(magic).ok());
    std::vector<u8> version = bytes;
    version[4] = 7;
    auto foreign = deserializeAnim(version, "dive.g7anim");
    REQUIRE_FALSE(foreign.ok());
    CHECK(foreign.error().message.find("version 7") != std::string::npos);
    for (const usize cut : {usize(0), usize(8), usize(12), bytes.size() / 2, bytes.size() - 1})
    {
        CAPTURE(cut);
        CHECK_FALSE(deserializeAnim(std::span(bytes).first(cut)).ok());
    }
    std::vector<u8> trailing = bytes;
    trailing.push_back(1);
    CHECK_FALSE(deserializeAnim(trailing).ok());
    std::vector<u8> bones = bytes;
    bones[10] = 0x7f;
    CHECK_FALSE(deserializeAnim(bones).ok());
    for (usize at = 12; at < bytes.size(); at += bytes.size() / 89 + 1)
    {
        std::vector<u8> damaged = bytes;
        damaged[at] ^= 0x5a;
        static_cast<void>(deserializeAnim(damaged));
    }
}

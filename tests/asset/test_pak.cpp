#include "TempDir.hpp"

#include <g7/asset/Pak.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/core/StringId.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <string_view>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
std::vector<u8> bytes(std::string_view s)
{
    return {s.begin(), s.end()};
}

u64 readU64(const std::vector<u8>& data, usize at)
{
    u64 v = 0;
    for (usize i = 0; i < 8; ++i)
    {
        v |= static_cast<u64>(data[at + i]) << (8 * i);
    }
    return v;
}

void writeU64(std::vector<u8>& data, usize at, u64 v)
{
    for (usize i = 0; i < 8; ++i)
    {
        data[at + i] = static_cast<u8>(v >> (8 * i));
    }
}

/// Writes raw bytes as an archive and tries to mount it.
Result<MountId> mountBytes(const test::TempDir& dir, const std::vector<u8>& data, Vfs& vfs)
{
    const fs::Path path = dir.path() / "test.g7pak";
    REQUIRE(fs::writeFile(path, data));
    return vfs.mount(path, 0);
}

PakWriter sampleWriter()
{
    PakWriter w;
    REQUIRE(w.add("meshes/Hut.glb", bytes("hut-data")));
    REQUIRE(w.add("textures\\wood.png", bytes("wood")));
    REQUIRE(w.add("empty.txt", {}));
    return w;
}
} // namespace

TEST_CASE("normalizeVfsPath")
{
    CHECK(normalizeVfsPath("meshes/hut.glb").value() == "meshes/hut.glb");
    CHECK(normalizeVfsPath("Meshes\\Hut.GLB").value() == "Meshes/Hut.GLB"); // spelling kept
    CHECK(normalizeVfsPath("/a//b/./c/").value() == "a/b/c");
    CHECK(normalizeVfsPath("worlds/Görlitz/haus.g7world").value() == "worlds/Görlitz/haus.g7world");

    CHECK_FALSE(normalizeVfsPath(""));
    CHECK_FALSE(normalizeVfsPath("/./"));
    CHECK_FALSE(normalizeVfsPath("a/../b"));
    CHECK_FALSE(normalizeVfsPath("C:/windows/file"));
    CHECK_FALSE(normalizeVfsPath(std::string("a\tb")));
}

TEST_CASE("PakWriter rejects invalid and duplicate paths")
{
    PakWriter w;
    CHECK(w.add("a/b.txt", bytes("1")));
    CHECK_FALSE(w.add("A\\B.TXT", bytes("2"))); // same path, different spelling
    CHECK_FALSE(w.add("../escape", bytes("3")));
    CHECK(w.entryCount() == 1);
}

TEST_CASE("pak layout: header, aligned data, sorted TOC, deterministic")
{
    const std::vector<u8> data = sampleWriter().serialize();
    REQUIRE(data.size() > kPakHeaderSize);
    CHECK(std::memcmp(data.data(), kPakMagic, 4) == 0);
    CHECK(data[4] == kPakVersion);
    CHECK(data[8] == 3); // entry count

    const u64 tocOffset = readU64(data, 16);
    const u64 tocSize = readU64(data, 24);
    CHECK(tocOffset + tocSize == data.size());
    // First TOC entry is the alphabetically first path ("empty.txt") at an aligned offset.
    CHECK(readU64(data, tocOffset) == StringId::hashOf("empty.txt"));
    CHECK(readU64(data, tocOffset + 8) % kPakAlignment == 0);

    // Insertion order does not matter.
    PakWriter reversed;
    REQUIRE(reversed.add("empty.txt", {}));
    REQUIRE(reversed.add("textures/wood.png", bytes("wood")));
    REQUIRE(reversed.add("meshes/Hut.glb", bytes("hut-data")));
    CHECK(reversed.serialize() == data);
}

TEST_CASE("pak round trip through the VFS")
{
    test::TempDir dir;
    const fs::Path pak = dir.path() / "base.g7pak";
    REQUIRE(sampleWriter().write(pak));

    Vfs vfs;
    REQUIRE(vfs.mount(pak, 0));
    CHECK(vfs.read("meshes/hut.glb").value() == bytes("hut-data"));
    CHECK(vfs.read("TEXTURES/WOOD.PNG").value() == bytes("wood"));
    CHECK(vfs.read("empty.txt").value().empty());
    CHECK(vfs.stat("meshes/hut.glb")->path == "meshes/Hut.glb");
    CHECK(vfs.stat("meshes/hut.glb")->size == 8);
    CHECK_FALSE(vfs.diskPath("meshes/hut.glb")); // inside an archive

    std::vector<u8> binary(1000);
    for (usize i = 0; i < binary.size(); ++i)
    {
        binary[i] = static_cast<u8>(i * 7);
    }
    PakWriter w;
    REQUIRE(w.add("bin/data.raw", binary));
    REQUIRE(w.write(dir.path() / "bin.g7pak"));
    REQUIRE(vfs.mount(dir.path() / "bin.g7pak", 0));
    CHECK(vfs.read("bin/data.raw").value() == binary);
}

TEST_CASE("corrupt archives are rejected with an error")
{
    test::TempDir dir;
    Vfs vfs;
    const std::vector<u8> good = sampleWriter().serialize();
    const u64 tocOffset = readU64(good, 16);

    SUBCASE("too small")
    {
        CHECK_FALSE(mountBytes(dir, bytes("G7PK"), vfs));
    }
    SUBCASE("bad magic")
    {
        auto data = good;
        data[0] = 'X';
        CHECK_FALSE(mountBytes(dir, data, vfs));
    }
    SUBCASE("unknown version")
    {
        auto data = good;
        data[4] = 99;
        CHECK_FALSE(mountBytes(dir, data, vfs));
    }
    SUBCASE("truncated")
    {
        auto data = good;
        data.resize(data.size() - 5);
        CHECK_FALSE(mountBytes(dir, data, vfs));
    }
    SUBCASE("data offset outside the data block")
    {
        auto data = good;
        writeU64(data, tocOffset + 8, tocOffset + 100);
        CHECK_FALSE(mountBytes(dir, data, vfs));
    }
    SUBCASE("path hash mismatch")
    {
        auto data = good;
        writeU64(data, tocOffset, 12345);
        CHECK_FALSE(mountBytes(dir, data, vfs));
    }
    SUBCASE("unknown entry flags")
    {
        auto data = good;
        data[tocOffset + 32] = 0x02;
        CHECK_FALSE(mountBytes(dir, data, vfs));
    }
    SUBCASE("duplicate paths")
    {
        // Build an archive whose second entry repeats the first path (same length: "a.txt"/"A.txt").
        PakWriter w;
        REQUIRE(w.add("a.txt", bytes("1")));
        REQUIRE(w.add("b.txt", bytes("2")));
        auto data = w.serialize();
        const u64 toc = readU64(data, 16);
        const usize secondPath = static_cast<usize>(toc) + (8 * 4 + 4 + 2 + 5) + (8 * 4 + 4 + 2);
        data[secondPath] = 'A';
        writeU64(data, static_cast<usize>(toc) + (8 * 4 + 4 + 2 + 5), StringId::hashOf("A.txt"));
        auto result = mountBytes(dir, data, vfs);
        REQUIRE_FALSE(result);
        CHECK(result.error().message.find("duplicate") != std::string::npos);
    }
    CHECK(vfs.mountCount() == 0);
}

namespace
{
/// Offsets of the first TOC entry's fields.
struct TocEntry
{
    usize at;
    usize offset() const { return at + 8; }
    usize size() const { return at + 16; }
    usize rawSize() const { return at + 24; }
    usize flags() const { return at + 32; }
};

TocEntry firstEntry(const std::vector<u8>& data)
{
    return {static_cast<usize>(readU64(data, 16))};
}

std::vector<u8> repetitive(usize n)
{
    std::vector<u8> out(n);
    for (usize i = 0; i < n; ++i)
    {
        out[i] = static_cast<u8>("Gothar Leonberg "[i % 16]);
    }
    return out;
}

std::vector<u8> noise(usize n)
{
    std::vector<u8> out(n);
    u32 x = 12345;
    for (auto& b : out)
    {
        x = x * 1664525u + 1013904223u; // LCG: incompressible enough for zstd
        b = static_cast<u8>(x >> 24);
    }
    return out;
}
} // namespace

TEST_CASE("zstd: compressible data is compressed and reads back unchanged")
{
    test::TempDir dir;
    const auto text = repetitive(20000);
    PakWriter w;
    REQUIRE(w.add("scripts/big.lua", text));
    const auto data = w.serialize();
    CHECK(data.size() < text.size() / 10);
    const TocEntry e = firstEntry(data);
    CHECK(data[e.flags()] == kPakFlagCompressed);
    CHECK(readU64(data, e.rawSize()) == text.size());

    Vfs vfs;
    REQUIRE(mountBytes(dir, data, vfs));
    CHECK(vfs.read("scripts/big.lua").value() == text);
    CHECK(vfs.stat("scripts/big.lua")->size == text.size()); // uncompressed size

    PakWriter again;
    REQUIRE(again.add("scripts/big.lua", text));
    CHECK(again.serialize() == data); // deterministic
}

TEST_CASE("zstd: Auto keeps already compressed and incompressible data raw")
{
    test::TempDir dir;
    const auto text = repetitive(4096);
    const auto random = noise(4096);
    PakWriter w;
    REQUIRE(w.add("textures/wood.png", text)); // extension says: compressed already
    REQUIRE(w.add("data/noise.bin", random));  // does not shrink
    REQUIRE(w.add("data/forced.bin", random, PakCompression::Zstd));
    REQUIRE(w.add("data/plain.txt", text, PakCompression::None));
    REQUIRE(w.add("data/empty.txt", {}));
    const auto data = w.serialize();

    // TOC order is alphabetical: empty, forced, noise, plain, wood.
    usize at = static_cast<usize>(readU64(data, 16));
    std::vector<u32> flags;
    for (int i = 0; i < 5; ++i)
    {
        flags.push_back(data[at + 32]);
        const u16 length = static_cast<u16>(data[at + 36] | (data[at + 37] << 8));
        at += 38 + length;
    }
    CHECK(flags == std::vector<u32>{0, kPakFlagCompressed, 0, 0, 0});

    Vfs vfs;
    REQUIRE(mountBytes(dir, data, vfs));
    CHECK(vfs.read("textures/wood.png").value() == text);
    CHECK(vfs.read("data/noise.bin").value() == random);
    CHECK(vfs.read("data/forced.bin").value() == random);
    CHECK(vfs.read("data/plain.txt").value() == text);
    CHECK(vfs.read("data/empty.txt").value().empty());
}

TEST_CASE("version 1 archives are still read")
{
    test::TempDir dir;
    PakWriter w;
    REQUIRE(w.add("a.txt", bytes("version one"), PakCompression::None));
    auto data = w.serialize();
    data[4] = 1;
    Vfs vfs;
    REQUIRE(mountBytes(dir, data, vfs));
    CHECK(vfs.read("a.txt").value() == bytes("version one"));

    // Version 1 never had compression.
    PakWriter compressed;
    REQUIRE(compressed.add("a.txt", repetitive(1000)));
    auto v1 = compressed.serialize();
    v1[4] = 1;
    Vfs other;
    CHECK_FALSE(mountBytes(dir, v1, other));
}

TEST_CASE("corrupt compressed entries")
{
    test::TempDir dir;
    PakWriter w;
    REQUIRE(w.add("a.txt", repetitive(5000)));
    const auto good = w.serialize();
    const TocEntry e = firstEntry(good);
    REQUIRE(good[e.flags()] == kPakFlagCompressed);

    SUBCASE("damaged zstd frame: mount works, read fails")
    {
        auto data = good;
        const usize offset = static_cast<usize>(readU64(data, e.offset()));
        for (usize i = 0; i < 8; ++i)
        {
            data[offset + 4 + i] ^= 0xFF;
        }
        Vfs vfs;
        REQUIRE(mountBytes(dir, data, vfs));
        auto read = vfs.read("a.txt");
        REQUIRE_FALSE(read);
        CHECK(read.error().message.find("corrupt entry") != std::string::npos);
    }
    SUBCASE("wrong raw size")
    {
        auto data = good;
        writeU64(data, e.rawSize(), 4999);
        Vfs vfs;
        REQUIRE(mountBytes(dir, data, vfs));
        CHECK_FALSE(vfs.read("a.txt"));
    }
    SUBCASE("absurd raw size is rejected at mount")
    {
        auto data = good;
        writeU64(data, e.rawSize(), kPakMaxEntrySize + 1);
        Vfs vfs;
        CHECK_FALSE(mountBytes(dir, data, vfs));
    }
}

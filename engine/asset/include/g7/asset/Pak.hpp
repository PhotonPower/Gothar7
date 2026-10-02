#pragma once

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// .g7pak archive format, version 2 (little-endian; ADR 0016):
///
///     Header (32 bytes)  magic "G7PK", version u32, entryCount u32, reserved u32,
///                        tocOffset u64, tocSize u64
///     Data               file contents, each starting at a 16-byte aligned offset
///     TOC                per entry: pathHash u64 (StringId::hashOf), offset u64, size u64,
///                        rawSize u64, flags u32, pathLength u16, path (UTF-8, normalised)
///
/// `size` is the stored size, `rawSize` the size after decompression. With kPakFlagCompressed the
/// stored data is one zstd frame; otherwise rawSize = size. Version 1 archives (never compressed)
/// are still read. Entries are sorted by path (case-insensitive), so the same input always
/// produces the same archive.
inline constexpr char kPakMagic[4] = {'G', '7', 'P', 'K'};
inline constexpr u32 kPakVersion = 2;
inline constexpr u32 kPakMinVersion = 1;
inline constexpr u64 kPakHeaderSize = 32;
inline constexpr u64 kPakAlignment = 16;
/// Entry flag: data is a zstd frame.
inline constexpr u32 kPakFlagCompressed = 1u << 0;
/// Largest decompressed entry a reader accepts (protects against absurd sizes in broken files).
inline constexpr u64 kPakMaxEntrySize = u64{1} << 31;
inline constexpr int kPakDefaultZstdLevel = 19;

enum class PakCompression : u8
{
    /// zstd unless the file is already compressed (.ktx2, .ogg, .png, .jpg, .jpeg) or does not
    /// shrink by at least 5 %.
    Auto,
    None,
    /// zstd even if it does not pay off.
    Zstd,
};

/// Builds a .g7pak archive in memory (used by tests and by g7-cook).
class PakWriter
{
public:
    /// Adds a file; the path is normalised. Fails on invalid paths and on duplicates
    /// (case-insensitive). Compression happens here, so memory holds the stored bytes only.
    [[nodiscard]] Result<void> add(std::string_view path, std::span<const u8> data,
                                   PakCompression compression = PakCompression::Auto);
    [[nodiscard]] usize entryCount() const noexcept { return m_entries.size(); }
    /// zstd level for later add() calls (1 = fast .. 22 = smallest); decompression speed does not depend on
    /// it.
    void setCompressionLevel(int level) noexcept { m_level = level; }

    /// The complete archive.
    [[nodiscard]] std::vector<u8> serialize() const;
    /// Writes the archive atomically (temporary file + rename).
    [[nodiscard]] Result<void> write(const fs::Path& target) const;

private:
    struct Entry
    {
        std::string path;
        std::string key; // lower-case path for sorting and duplicate checks
        std::vector<u8> stored;
        u64 rawSize = 0;
        u32 flags = 0;
    };
    std::vector<Entry> m_entries;
    int m_level = kPakDefaultZstdLevel;
};
} // namespace g7::asset

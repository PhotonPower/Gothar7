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
/// .g7pak archive format, version 1 (little-endian):
///
///     Header (32 bytes)  magic "G7PK", version u32, entryCount u32, reserved u32,
///                        tocOffset u64, tocSize u64
///     Data               file contents, each starting at a 16-byte aligned offset
///     TOC                per entry: pathHash u64 (StringId::hashOf), offset u64, size u64,
///                        rawSize u64, flags u32, pathLength u16, path (UTF-8, normalised)
///
/// Version 1 stores data uncompressed (flags = 0, rawSize = size). Entries are sorted by path
/// (case-insensitive), so the same input always produces the same archive.
inline constexpr char kPakMagic[4] = {'G', '7', 'P', 'K'};
inline constexpr u32 kPakVersion = 1;
inline constexpr u64 kPakHeaderSize = 32;
inline constexpr u64 kPakAlignment = 16;
/// Entry flag reserved for compressed data (LZ4/Zstd, decided together with the cooker).
inline constexpr u32 kPakFlagCompressed = 1u << 0;

/// Builds a .g7pak archive in memory (used by tests now and by g7-cook in M3).
class PakWriter
{
public:
    /// Adds a file; the path is normalised. Fails on invalid paths and on duplicates
    /// (case-insensitive).
    [[nodiscard]] Result<void> add(std::string_view path, std::span<const u8> data);
    [[nodiscard]] usize entryCount() const noexcept { return m_entries.size(); }

    /// The complete archive.
    [[nodiscard]] std::vector<u8> serialize() const;
    /// Writes the archive atomically (temporary file + rename).
    [[nodiscard]] Result<void> write(const fs::Path& target) const;

private:
    struct Entry
    {
        std::string path;
        std::string key; // lower-case path for sorting and duplicate checks
        std::vector<u8> data;
    };
    std::vector<Entry> m_entries;
};
} // namespace g7::asset

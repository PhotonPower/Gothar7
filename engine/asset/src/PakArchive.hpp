#pragma once

// Internal: read access to .g7pak archives (format in g7/asset/Pak.hpp).

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <string>
#include <vector>

namespace g7::asset::detail
{
struct PakEntry
{
    std::string path; // normalised, original spelling
    u64 offset = 0;
    u64 storedSize = 0; // bytes in the archive
    u64 size = 0;       // bytes after decompression (what read() returns)
    bool compressed = false;
};

/// Table of contents of an archive; data is read on demand, so instances are cheap to share and
/// read() is safe to call from several threads (each call opens its own stream).
class PakArchive
{
public:
    /// Reads and validates header and TOC (magic, version, bounds, hashes, flags, duplicates).
    [[nodiscard]] static Result<PakArchive> open(const fs::Path& path);

    [[nodiscard]] const fs::Path& path() const noexcept { return m_path; }
    [[nodiscard]] const std::vector<PakEntry>& entries() const noexcept { return m_entries; }
    /// Reads (and decompresses) one entry.
    [[nodiscard]] Result<std::vector<u8>> read(const PakEntry& entry) const;

private:
    fs::Path m_path;
    std::vector<PakEntry> m_entries;
};
} // namespace g7::asset::detail

#include "ByteIo.hpp"
#include "PakArchive.hpp"

#include <g7/asset/Pak.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/core/Assert.hpp>
#include <g7/core/StringId.hpp>
#include <g7/core/StringUtil.hpp>

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace g7::asset
{
namespace
{
// Smallest TOC entry: hash, offset, size, rawSize (4 x u64), flags u32, pathLength u16.
constexpr u64 kTocEntryFixedSize = 8 * 4 + 4 + 2;

void setU64(std::vector<u8>& out, usize at, u64 v)
{
    for (int i = 0; i < 8; ++i)
    {
        out[at + static_cast<usize>(i)] = static_cast<u8>(v >> (8 * i));
    }
}

using detail::ByteReader;
using detail::ByteWriter;

Error pakError(const fs::Path& path, const std::string& what)
{
    return Error{fs::toUtf8(path) + ": " + what};
}

/// Formats that are compressed already; zstd would only cost time.
bool alreadyCompressed(std::string_view path)
{
    static constexpr std::string_view kExtensions[] = {".ktx2", ".ogg", ".png", ".jpg", ".jpeg"};
    return std::any_of(std::begin(kExtensions), std::end(kExtensions),
                       [&](std::string_view ext)
                       {
                           return path.size() >= ext.size() &&
                                  equalsIgnoreCase(path.substr(path.size() - ext.size()), ext);
                       });
}

std::vector<u8> zstdCompress(std::span<const u8> data, int level)
{
    std::vector<u8> out(ZSTD_compressBound(data.size()));
    const size_t written = ZSTD_compress(out.data(), out.size(), data.data(), data.size(), level);
    G7_VERIFY(!ZSTD_isError(written), "zstd compression failed");
    out.resize(written);
    return out;
}
} // namespace

Result<void> PakWriter::add(std::string_view path, std::span<const u8> data, PakCompression compression)
{
    auto normalised = normalizeVfsPath(path);
    if (!normalised)
    {
        return normalised.error();
    }
    if (normalised.value().size() > std::numeric_limits<u16>::max())
    {
        return Error{"pak path too long: " + normalised.value()};
    }
    std::string key = toLower(normalised.value());
    const bool duplicate =
        std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.key == key; });
    if (duplicate)
    {
        return Error{"duplicate pak path: " + normalised.value()};
    }
    if (data.size() > kPakMaxEntrySize)
    {
        return Error{"pak entry too large: " + normalised.value()};
    }

    Entry entry{std::move(normalised).value(), std::move(key), {}, data.size(), 0};
    const bool tryZstd =
        compression == PakCompression::Zstd ||
        (compression == PakCompression::Auto && !data.empty() && !alreadyCompressed(entry.path));
    if (tryZstd)
    {
        std::vector<u8> packed = zstdCompress(data, m_level);
        // Auto keeps the raw bytes unless compression saves at least 5 %.
        if (compression == PakCompression::Zstd || packed.size() * 20 <= data.size() * 19)
        {
            entry.stored = std::move(packed);
            entry.flags = kPakFlagCompressed;
        }
    }
    if (entry.flags == 0)
    {
        entry.stored.assign(data.begin(), data.end());
    }
    m_entries.push_back(std::move(entry));
    return {};
}

std::vector<u8> PakWriter::serialize() const
{
    std::vector<const Entry*> sorted;
    sorted.reserve(m_entries.size());
    for (const Entry& e : m_entries)
    {
        sorted.push_back(&e);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Entry* a, const Entry* b) { return a->key < b->key; });

    std::vector<u8> out;
    ByteWriter w(out);
    w.text(std::string_view(kPakMagic, sizeof(kPakMagic)));
    w.u32v(kPakVersion);
    w.u32v(static_cast<u32>(sorted.size()));
    w.u32v(0); // reserved
    w.u64v(0); // tocOffset, patched below
    w.u64v(0); // tocSize, patched below

    std::vector<u64> offsets;
    offsets.reserve(sorted.size());
    for (const Entry* e : sorted)
    {
        out.resize((out.size() + kPakAlignment - 1) / kPakAlignment * kPakAlignment, 0);
        offsets.push_back(out.size());
        out.insert(out.end(), e->stored.begin(), e->stored.end());
    }

    const u64 tocOffset = out.size();
    for (usize i = 0; i < sorted.size(); ++i)
    {
        const Entry& e = *sorted[i];
        w.u64v(StringId::hashOf(e.path));
        w.u64v(offsets[i]);
        w.u64v(e.stored.size());
        w.u64v(e.rawSize);
        w.u32v(e.flags);
        w.string16(e.path);
    }
    setU64(out, 16, tocOffset);
    setU64(out, 24, out.size() - tocOffset);
    return out;
}

Result<void> PakWriter::write(const fs::Path& target) const
{
    return fs::writeFileAtomic(target, serialize());
}

namespace detail
{
Result<PakArchive> PakArchive::open(const fs::Path& path)
{
    std::error_code ec;
    const u64 fileSize = std::filesystem::file_size(path, ec);
    if (ec)
    {
        return pakError(path, "cannot open archive (" + ec.message() + ")");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return pakError(path, "cannot open archive");
    }
    if (fileSize < kPakHeaderSize)
    {
        return pakError(path, "not a .g7pak archive (file too small)");
    }

    u8 headerBytes[kPakHeaderSize];
    in.read(reinterpret_cast<char*>(headerBytes), static_cast<std::streamsize>(kPakHeaderSize));
    if (!in || std::memcmp(headerBytes, kPakMagic, sizeof(kPakMagic)) != 0)
    {
        return pakError(path, "not a .g7pak archive (bad magic)");
    }
    ByteReader header(std::span<const u8>(headerBytes, kPakHeaderSize));
    (void)header.text(sizeof(kPakMagic));
    const u32 version = header.u32v();
    const u32 entryCount = header.u32v();
    (void)header.u32v(); // reserved
    const u64 tocOffset = header.u64v();
    const u64 tocSize = header.u64v();
    if (version < kPakMinVersion || version > kPakVersion)
    {
        return pakError(path, "unsupported .g7pak version " + std::to_string(version));
    }
    if (tocOffset < kPakHeaderSize || tocOffset > fileSize || tocSize > fileSize - tocOffset)
    {
        return pakError(path, "corrupt archive (table of contents outside the file)");
    }
    if (entryCount > tocSize / kTocEntryFixedSize)
    {
        return pakError(path, "corrupt archive (entry count does not fit the table of contents)");
    }

    std::vector<u8> toc(static_cast<usize>(tocSize));
    in.seekg(static_cast<std::streamoff>(tocOffset));
    in.read(reinterpret_cast<char*>(toc.data()), static_cast<std::streamsize>(tocSize));
    if (!in)
    {
        return pakError(path, "cannot read table of contents");
    }

    PakArchive archive;
    archive.m_path = path;
    archive.m_entries.reserve(entryCount);
    std::unordered_set<std::string> keys;
    ByteReader r(toc);
    for (u32 i = 0; i < entryCount; ++i)
    {
        if (!r.has(kTocEntryFixedSize))
        {
            return pakError(path, "corrupt archive (truncated table of contents)");
        }
        const u64 hash = r.u64v();
        const u64 offset = r.u64v();
        const u64 size = r.u64v();
        const u64 rawSize = r.u64v();
        const u32 flags = r.u32v();
        const u16 pathLength = r.u16v();
        if (!r.has(pathLength))
        {
            return pakError(path, "corrupt archive (truncated table of contents)");
        }
        const std::string entryPath(r.text(pathLength));
        const std::string where = " (entry '" + entryPath + "')";

        auto normalised = normalizeVfsPath(entryPath);
        if (!normalised || normalised.value() != entryPath)
        {
            return pakError(path, "corrupt archive (invalid path)" + where);
        }
        if (hash != StringId::hashOf(entryPath))
        {
            return pakError(path, "corrupt archive (path hash mismatch)" + where);
        }
        const bool compressed = (flags & kPakFlagCompressed) != 0;
        if ((flags & ~kPakFlagCompressed) != 0 || (compressed && version < 2))
        {
            return pakError(path, "corrupt archive (unknown flags)" + where);
        }
        if (compressed ? rawSize > kPakMaxEntrySize : rawSize != size)
        {
            return pakError(path, "corrupt archive (size mismatch)" + where);
        }
        if (offset < kPakHeaderSize || offset > tocOffset || size > tocOffset - offset)
        {
            return pakError(path, "corrupt archive (data outside the data block)" + where);
        }
        if (!keys.insert(toLower(entryPath)).second)
        {
            return pakError(path, "corrupt archive (duplicate path)" + where);
        }
        archive.m_entries.push_back({entryPath, offset, size, rawSize, compressed});
    }
    return archive;
}

Result<std::vector<u8>> PakArchive::read(const PakEntry& entry) const
{
    std::ifstream in(m_path, std::ios::binary);
    if (!in)
    {
        return pakError(m_path, "cannot open archive");
    }
    std::vector<u8> stored(static_cast<usize>(entry.storedSize));
    in.seekg(static_cast<std::streamoff>(entry.offset));
    in.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
    if (!in && !stored.empty())
    {
        return pakError(m_path, "cannot read '" + entry.path + "'");
    }
    if (!entry.compressed)
    {
        return stored;
    }
    std::vector<u8> data(static_cast<usize>(entry.size));
    const size_t result = ZSTD_decompress(data.data(), data.size(), stored.data(), stored.size());
    if (ZSTD_isError(result) || result != data.size())
    {
        return pakError(m_path, "corrupt entry '" + entry.path + "' (" +
                                    (ZSTD_isError(result) ? ZSTD_getErrorName(result) : "size mismatch") +
                                    ")");
    }
    return data;
}
} // namespace detail
} // namespace g7::asset

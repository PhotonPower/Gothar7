#include "PakArchive.hpp"

#include <g7/asset/Pak.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/core/StringId.hpp>
#include <g7/core/StringUtil.hpp>

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

void putU16(std::vector<u8>& out, u16 v)
{
    out.push_back(static_cast<u8>(v));
    out.push_back(static_cast<u8>(v >> 8));
}

void putU32(std::vector<u8>& out, u32 v)
{
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<u8>(v >> (8 * i)));
    }
}

void putU64(std::vector<u8>& out, u64 v)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<u8>(v >> (8 * i)));
    }
}

void setU64(std::vector<u8>& out, usize at, u64 v)
{
    for (int i = 0; i < 8; ++i)
    {
        out[at + static_cast<usize>(i)] = static_cast<u8>(v >> (8 * i));
    }
}

/// Little-endian reader over a byte span with bounds checks.
class ByteReader
{
public:
    explicit ByteReader(std::span<const u8> bytes) : m_bytes(bytes) {}

    [[nodiscard]] bool has(u64 n) const noexcept { return n <= m_bytes.size() - m_pos; }
    [[nodiscard]] u64 position() const noexcept { return m_pos; }

    template <typename T>
    [[nodiscard]] T get() noexcept
    {
        T v = 0;
        for (usize i = 0; i < sizeof(T); ++i)
        {
            v |= static_cast<T>(static_cast<T>(m_bytes[m_pos + i]) << (8 * i));
        }
        m_pos += sizeof(T);
        return v;
    }

    [[nodiscard]] std::string_view text(usize n) noexcept
    {
        std::string_view s(reinterpret_cast<const char*>(m_bytes.data() + m_pos), n);
        m_pos += n;
        return s;
    }

private:
    std::span<const u8> m_bytes;
    usize m_pos = 0;
};

Error pakError(const fs::Path& path, const std::string& what)
{
    return Error{fs::toUtf8(path) + ": " + what};
}
} // namespace

Result<void> PakWriter::add(std::string_view path, std::span<const u8> data)
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
    m_entries.push_back({std::move(normalised).value(), std::move(key), {data.begin(), data.end()}});
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
    out.insert(out.end(), std::begin(kPakMagic), std::end(kPakMagic));
    putU32(out, kPakVersion);
    putU32(out, static_cast<u32>(sorted.size()));
    putU32(out, 0); // reserved
    putU64(out, 0); // tocOffset, patched below
    putU64(out, 0); // tocSize, patched below

    std::vector<u64> offsets;
    offsets.reserve(sorted.size());
    for (const Entry* e : sorted)
    {
        out.resize((out.size() + kPakAlignment - 1) / kPakAlignment * kPakAlignment, 0);
        offsets.push_back(out.size());
        out.insert(out.end(), e->data.begin(), e->data.end());
    }

    const u64 tocOffset = out.size();
    for (usize i = 0; i < sorted.size(); ++i)
    {
        const Entry& e = *sorted[i];
        putU64(out, StringId::hashOf(e.path));
        putU64(out, offsets[i]);
        putU64(out, e.data.size());
        putU64(out, e.data.size()); // rawSize: uncompressed in version 1
        putU32(out, 0);             // flags
        putU16(out, static_cast<u16>(e.path.size()));
        out.insert(out.end(), e.path.begin(), e.path.end());
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
    const u32 version = header.get<u32>();
    const u32 entryCount = header.get<u32>();
    (void)header.get<u32>(); // reserved
    const u64 tocOffset = header.get<u64>();
    const u64 tocSize = header.get<u64>();
    if (version != kPakVersion)
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
        const u64 hash = r.get<u64>();
        const u64 offset = r.get<u64>();
        const u64 size = r.get<u64>();
        const u64 rawSize = r.get<u64>();
        const u32 flags = r.get<u32>();
        const u16 pathLength = r.get<u16>();
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
        if ((flags & kPakFlagCompressed) != 0)
        {
            return pakError(path, "compressed entries are not supported yet" + where);
        }
        if (flags != 0 || rawSize != size)
        {
            return pakError(path, "corrupt archive (unknown flags or size mismatch)" + where);
        }
        if (offset < kPakHeaderSize || offset > tocOffset || size > tocOffset - offset)
        {
            return pakError(path, "corrupt archive (data outside the data block)" + where);
        }
        if (!keys.insert(toLower(entryPath)).second)
        {
            return pakError(path, "corrupt archive (duplicate path)" + where);
        }
        archive.m_entries.push_back({entryPath, offset, size});
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
    std::vector<u8> data(static_cast<usize>(entry.size));
    in.seekg(static_cast<std::streamoff>(entry.offset));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in && !data.empty())
    {
        return pakError(m_path, "cannot read '" + entry.path + "'");
    }
    return data;
}
} // namespace detail
} // namespace g7::asset

#include <g7/core/Crc32.hpp>
#include <g7/core/Log.hpp>
#include <g7/save/SaveFile.hpp>

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <cstring>
#include <format>

namespace g7::save
{
namespace
{
using Json = nlohmann::json;

constexpr u32 kHeaderSize = 20;
constexpr u32 kChunkHeaderSize = 20;
constexpr u16 kChunkZstd = 1;
constexpr int kZstdLevel = 3;
/// A chunk larger than this is refused (a broken size field must not allocate gigabytes).
constexpr u32 kMaxChunkBytes = 256u * 1024u * 1024u;

constexpr u32 tag(const char (&name)[5]) noexcept
{
    return static_cast<u32>(static_cast<u8>(name[0])) | static_cast<u32>(static_cast<u8>(name[1])) << 8 |
           static_cast<u32>(static_cast<u8>(name[2])) << 16 |
           static_cast<u32>(static_cast<u8>(name[3])) << 24;
}
constexpr u32 kMagic = tag("G7SV");
constexpr u32 kMeta = tag("META");
constexpr u32 kThumbnail = tag("THMB");
constexpr u32 kGlobal = tag("GLOB");
constexpr u32 kWorld = tag("WRLD");
/// Chunk versions this game writes (and reads up to).
constexpr u16 kMetaVersion = 1;
constexpr u16 kThumbnailVersion = 1;
constexpr u16 kGlobalVersion = 1;
constexpr u16 kWorldVersion = 1;

std::string tagName(u32 t)
{
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i)
    {
        s[static_cast<usize>(i)] = static_cast<char>((t >> (8 * i)) & 0xFF);
    }
    return s;
}

void put16(std::vector<u8>& out, u16 v)
{
    out.push_back(static_cast<u8>(v));
    out.push_back(static_cast<u8>(v >> 8));
}

void put32(std::vector<u8>& out, u32 v)
{
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<u8>(v >> (8 * i)));
    }
}

u16 get16(const u8* p)
{
    return static_cast<u16>(p[0] | p[1] << 8);
}

u32 get32(const u8* p)
{
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

struct Chunk
{
    u32 tag = 0;
    u16 version = 1;
    std::vector<u8> data; ///< unpacked
};

std::span<const u8> bytesOf(std::string_view text)
{
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

Result<void> writeChunk(std::vector<u8>& out, u32 t, u16 version, std::span<const u8> raw, bool pack)
{
    std::vector<u8> stored;
    u16 flags = 0;
    if (pack)
    {
        stored.resize(ZSTD_compressBound(raw.size()));
        const usize size = ZSTD_compress(stored.data(), stored.size(), raw.data(), raw.size(), kZstdLevel);
        if (ZSTD_isError(size) != 0)
        {
            return Error{std::format("chunk {}: {}", tagName(t), ZSTD_getErrorName(size))};
        }
        stored.resize(size);
        flags = kChunkZstd;
    }
    else
    {
        stored.assign(raw.begin(), raw.end());
    }
    if (raw.size() > kMaxChunkBytes || stored.size() > kMaxChunkBytes)
    {
        return Error{std::format("chunk {} is too large ({} bytes)", tagName(t), raw.size())};
    }
    const usize start = out.size();
    put32(out, t);
    put16(out, version);
    put16(out, flags);
    put32(out, static_cast<u32>(raw.size()));
    put32(out, static_cast<u32>(stored.size()));
    // The CRC covers the chunk's head (tag, version, flags, sizes) and its data.
    put32(out, crc32(stored, crc32(std::span<const u8>(out.data() + start, 16))));
    out.insert(out.end(), stored.begin(), stored.end());
    return {};
}

std::string metaJson(const SaveMeta& m)
{
    return Json{{"name", m.name},
                {"world", m.world},
                {"place", m.place},
                {"language", m.language},
                {"gameVersion", m.gameVersion},
                {"savedAtUtc", m.savedAtUtc},
                {"playSeconds", m.playSeconds},
                {"day", m.day},
                {"minute", m.minute},
                {"chapter", m.chapter},
                {"heroLevel", m.heroLevel},
                {"contentStamp", m.contentStamp}}
        .dump();
}

Result<SaveMeta> parseMeta(std::span<const u8> data)
{
    const Json j = Json::parse(data.begin(), data.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        return Error{"META: not a JSON object"};
    }
    SaveMeta m;
    try
    {
        m.name = j.at("name").get<std::string>();
        m.world = j.at("world").get<std::string>();
        m.place = j.at("place").get<std::string>();
        m.language = j.at("language").get<std::string>();
        m.gameVersion = j.at("gameVersion").get<std::string>();
        m.savedAtUtc = j.at("savedAtUtc").get<i64>();
        m.playSeconds = j.at("playSeconds").get<f64>();
        m.day = j.at("day").get<u32>();
        m.minute = j.at("minute").get<u32>();
        m.chapter = j.at("chapter").get<u32>();
        m.heroLevel = j.at("heroLevel").get<i32>();
        m.contentStamp = j.at("contentStamp").get<u64>();
    }
    catch (const Json::exception& e)
    {
        return Error{std::format("META: {}", e.what())};
    }
    return m;
}

/// Walks the chunks; `stopAfter` > 0: only that many (the list of slots needs META and THMB). Every chunk
/// read is checked (CRC, sizes, version) and unpacked.
Result<std::vector<Chunk>> readChunks(std::span<const u8> bytes, u32 stopAfter = 0)
{
    if (bytes.size() < kHeaderSize || get32(bytes.data()) != kMagic)
    {
        return Error{"not a save file"};
    }
    if (crc32(bytes.first(16)) != get32(bytes.data() + 16))
    {
        return Error{"damaged (header)"};
    }
    const u16 format = get16(bytes.data() + 4);
    if (format > kSaveFormat)
    {
        return Error{std::format("from a newer version (format {})", format)};
    }
    const u32 count = get32(bytes.data() + 8);
    std::vector<Chunk> chunks;
    usize at = kHeaderSize;
    for (u32 i = 0; i < count && (stopAfter == 0 || i < stopAfter); ++i)
    {
        if (bytes.size() - at < kChunkHeaderSize)
        {
            return Error{std::format("damaged (chunk {} cut off)", i)};
        }
        const u8* h = bytes.data() + at;
        Chunk c;
        c.tag = get32(h);
        c.version = get16(h + 4);
        const u16 flags = get16(h + 6);
        const u32 rawSize = get32(h + 8);
        const u32 size = get32(h + 12);
        const u32 crc = get32(h + 16);
        at += kChunkHeaderSize;
        if (size > kMaxChunkBytes || rawSize > kMaxChunkBytes || bytes.size() - at < size)
        {
            return Error{std::format("damaged (chunk {} {} cut off)", i, tagName(c.tag))};
        }
        const std::span<const u8> stored = bytes.subspan(at, size);
        at += size;
        if (crc32(stored, crc32(std::span<const u8>(h, 16))) != crc ||
            ((flags & kChunkZstd) == 0 && rawSize != size))
        {
            return Error{std::format("damaged (chunk {} {})", i, tagName(c.tag))};
        }
        if ((flags & kChunkZstd) != 0)
        {
            c.data.resize(rawSize);
            const usize n = ZSTD_decompress(c.data.data(), c.data.size(), stored.data(), stored.size());
            if (ZSTD_isError(n) != 0 || n != rawSize)
            {
                return Error{std::format("damaged (chunk {} {} does not unpack)", i, tagName(c.tag))};
            }
        }
        else
        {
            c.data.assign(stored.begin(), stored.end());
        }
        chunks.push_back(std::move(c));
    }
    return chunks;
}

std::string_view textOf(const Chunk& c)
{
    return {reinterpret_cast<const char*>(c.data.data()), c.data.size()};
}

Result<void> checkVersion(const Chunk& c, u16 known)
{
    if (c.version > known)
    {
        return Error{std::format("from a newer version (chunk {} v{})", tagName(c.tag), c.version)};
    }
    // Older chunk versions are migrated here (none yet: every chunk is v1).
    return {};
}
} // namespace

Result<std::vector<u8>> encodeSave(const SaveFile& save)
{
    std::vector<u8> out;
    put32(out, kMagic);
    put16(out, kSaveFormat);
    put16(out, 0);
    put32(out, static_cast<u32>(2 + 1 + save.state.worlds.size()));
    put32(out, 0);
    put32(out, crc32(std::span<const u8>(out.data(), 16)));
    const std::string meta = metaJson(save.meta);
    const std::string global = globalJson(save.state);
    for (const auto& [t, version, raw, pack] :
         {std::tuple{kMeta, kMetaVersion, bytesOf(meta), false},
          std::tuple{kThumbnail, kThumbnailVersion, std::span<const u8>(save.thumbnailPng), false},
          std::tuple{kGlobal, kGlobalVersion, bytesOf(global), true}})
    {
        if (auto written = writeChunk(out, t, version, raw, pack); !written)
        {
            return written.error();
        }
    }
    for (const WorldState& w : save.state.worlds)
    {
        const std::string world = worldJson(w);
        if (auto written = writeChunk(out, kWorld, kWorldVersion, bytesOf(world), true); !written)
        {
            return written.error();
        }
    }
    return out;
}

Result<SaveFile> decodeSave(std::span<const u8> bytes)
{
    auto chunks = readChunks(bytes);
    if (!chunks)
    {
        return chunks.error();
    }
    SaveFile save;
    bool haveMeta = false;
    bool haveGlobal = false;
    for (const Chunk& c : chunks.value())
    {
        if (c.tag == kMeta || c.tag == kThumbnail || c.tag == kGlobal || c.tag == kWorld)
        {
            const u16 known = c.tag == kMeta        ? kMetaVersion
                              : c.tag == kThumbnail ? kThumbnailVersion
                              : c.tag == kGlobal    ? kGlobalVersion
                                                    : kWorldVersion;
            if (auto ok = checkVersion(c, known); !ok)
            {
                return ok.error();
            }
        }
        if (c.tag == kMeta)
        {
            auto meta = parseMeta(c.data);
            if (!meta)
            {
                return meta.error();
            }
            save.meta = std::move(meta).value();
            haveMeta = true;
        }
        else if (c.tag == kThumbnail)
        {
            save.thumbnailPng = c.data;
        }
        else if (c.tag == kGlobal)
        {
            auto global = parseGlobalJson(textOf(c));
            if (!global)
            {
                return global.error();
            }
            std::vector<WorldState> worlds = std::move(save.state.worlds);
            save.state = std::move(global).value();
            save.state.worlds = std::move(worlds);
            haveGlobal = true;
        }
        else if (c.tag == kWorld)
        {
            auto world = parseWorldJson(textOf(c), std::format("world {}", save.state.worlds.size()));
            if (!world)
            {
                return world.error();
            }
            save.state.worlds.push_back(std::move(world).value());
        }
        else
        {
            G7_LOG_WARN("save", "unknown chunk {} skipped", tagName(c.tag));
        }
    }
    if (!haveMeta || !haveGlobal)
    {
        return Error{"damaged (META or GLOB missing)"};
    }
    return save;
}

Result<SaveMeta> decodeMeta(std::span<const u8> bytes, std::vector<u8>* thumbnail)
{
    auto chunks = readChunks(bytes, 2);
    if (!chunks)
    {
        return chunks.error();
    }
    if (chunks.value().empty() || chunks.value()[0].tag != kMeta)
    {
        return Error{"damaged (META missing)"};
    }
    if (auto ok = checkVersion(chunks.value()[0], kMetaVersion); !ok)
    {
        return ok.error();
    }
    if (thumbnail != nullptr)
    {
        thumbnail->clear();
        if (chunks.value().size() > 1 && chunks.value()[1].tag == kThumbnail)
        {
            *thumbnail = chunks.value()[1].data;
        }
    }
    return parseMeta(chunks.value()[0].data);
}

Result<void> writeSaveFile(const fs::Path& path, const SaveFile& save)
{
    auto bytes = encodeSave(save);
    if (!bytes)
    {
        return bytes.error();
    }
    if (path.has_parent_path())
    {
        if (auto made = fs::createDirectories(path.parent_path()); !made)
        {
            return made;
        }
    }
    return fs::writeFileAtomic(path, bytes.value());
}

Result<SaveFile> readSaveFile(const fs::Path& path)
{
    auto bytes = fs::readFile(path);
    if (!bytes)
    {
        return bytes.error();
    }
    return decodeSave(bytes.value());
}

Result<SaveMeta> readSaveMeta(const fs::Path& path, std::vector<u8>* thumbnail)
{
    // The whole file is read (a few hundred kB), but only META and THMB are checked and unpacked.
    auto bytes = fs::readFile(path);
    if (!bytes)
    {
        return bytes.error();
    }
    return decodeMeta(bytes.value(), thumbnail);
}

fs::Path slotPath(const fs::Path& directory, std::string_view slot)
{
    return directory / fs::fromUtf8(std::string(slot) + ".g7save");
}

std::vector<SlotInfo> listSlots(const fs::Path& directory)
{
    std::vector<std::string> names;
    for (u32 i = 1; i <= kSaveSlots; ++i)
    {
        names.push_back(std::format("slot_{:02}", i));
    }
    names.emplace_back("quick");
    names.emplace_back("auto");
    std::vector<SlotInfo> slots;
    for (const std::string& name : names)
    {
        SlotInfo info;
        info.slot = name;
        info.path = slotPath(directory, name);
        if (fs::exists(info.path))
        {
            auto meta = readSaveMeta(info.path, &info.thumbnailPng);
            if (meta)
            {
                info.meta = std::move(meta).value();
            }
            else
            {
                info.error = meta.error().message;
            }
        }
        slots.push_back(std::move(info));
    }
    return slots;
}
} // namespace g7::save

#include "Manifest.hpp"

#include <g7/asset/Vfs.hpp>

#include <charconv>
#include <cstring>

namespace g7::cook
{
namespace
{
constexpr std::string_view kMagic = "g7cook-manifest";
constexpr u32 kFormatVersion = 1;

std::string hex(u64 v)
{
    char buf[17];
    for (int i = 15; i >= 0; --i)
    {
        buf[i] = "0123456789abcdef"[v & 0xF];
        v >>= 4;
    }
    buf[16] = '\0';
    return buf;
}

bool parseHex(std::string_view s, u64& out)
{
    if (s.empty() || s.size() > 16)
    {
        return false;
    }
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out, 16);
    return ec == std::errc() && end == s.data() + s.size();
}

/// Splits at tabs; the last field keeps everything after the (n-1)th tab.
std::vector<std::string_view> fields(std::string_view line, usize n)
{
    std::vector<std::string_view> out;
    while (out.size() + 1 < n)
    {
        const auto tab = line.find('\t');
        if (tab == std::string_view::npos)
        {
            break;
        }
        out.push_back(line.substr(0, tab));
        line.remove_prefix(tab + 1);
    }
    out.push_back(line);
    return out;
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

std::string percentDecode(std::string_view s)
{
    std::string out;
    for (usize i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size() && hexDigit(s[i + 1]) >= 0 && hexDigit(s[i + 2]) >= 0)
        {
            out += static_cast<char>(hexDigit(s[i + 1]) * 16 + hexDigit(s[i + 2]));
            i += 2;
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

/// Manifest paths must be normalised relative VFS paths: no "..", no absolute or drive paths.
/// A manifest is data from disk; without this check it could make the cooker read or delete
/// files outside the output directory.
bool isSafePath(std::string_view path)
{
    auto normalised = asset::normalizeVfsPath(path);
    return normalised && normalised.value() == path;
}

/// JSON text of a .gltf, or of the JSON chunk of a .glb.
std::string_view gltfJson(std::span<const u8> bytes)
{
    const auto u32At = [&](usize at)
    {
        return static_cast<u32>(bytes[at]) | (static_cast<u32>(bytes[at + 1]) << 8) |
               (static_cast<u32>(bytes[at + 2]) << 16) | (static_cast<u32>(bytes[at + 3]) << 24);
    };
    if (bytes.size() >= 20 && std::memcmp(bytes.data(), "glTF", 4) == 0)
    {
        const u32 length = u32At(12);
        if (u32At(16) != 0x4E4F534A || length > bytes.size() - 20) // "JSON"
        {
            return {};
        }
        return {reinterpret_cast<const char*>(bytes.data() + 20), length};
    }
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
} // namespace

u64 hashBytes(std::span<const u8> data, u64 seed) noexcept
{
    u64 h = seed;
    for (const u8 b : data)
    {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

u64 hashText(std::string_view text, u64 seed) noexcept
{
    return hashBytes(std::span(reinterpret_cast<const u8*>(text.data()), text.size()), seed);
}

std::string serializeManifest(const Manifest& manifest)
{
    std::string out;
    out += std::string(kMagic) + "\t" + std::to_string(kFormatVersion) + "\n";
    out += "cooker\t" + std::to_string(manifest.cookerVersion) + "\n";
    out += "options\t" + manifest.options + "\n";
    for (const auto& [path, entry] : manifest.sources)
    {
        out += "source\t" + hex(entry.key) + "\t" + path + "\n";
        for (const auto& d : entry.dependencies)
        {
            out += "dep\t" + hex(d.hash) + "\t" + d.path + "\n";
        }
        for (const auto& o : entry.outputs)
        {
            out += "out\t" + hex(o.hash) + "\t" + std::to_string(o.size) + "\t" + o.path + "\n";
        }
    }
    return out;
}

Result<Manifest> parseManifest(std::string_view text)
{
    Manifest manifest;
    ManifestEntry* current = nullptr;
    usize lineNumber = 0;
    bool headerSeen = false;
    while (!text.empty())
    {
        const auto newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (line.empty())
        {
            continue;
        }
        const auto bad = [&] { return Error{"manifest line " + std::to_string(lineNumber) + " is invalid"}; };
        const auto tag = line.substr(0, line.find('\t'));
        if (!headerSeen)
        {
            const auto f = fields(line, 2);
            if (f.size() != 2 || f[0] != kMagic || f[1] != std::to_string(kFormatVersion))
            {
                return Error{"not a g7cook manifest (or unknown format version)"};
            }
            headerSeen = true;
        }
        else if (tag == "cooker")
        {
            const auto f = fields(line, 2);
            u32 version = 0;
            const auto [end, ec] = std::from_chars(f[1].data(), f[1].data() + f[1].size(), version);
            if (f.size() != 2 || ec != std::errc() || end != f[1].data() + f[1].size())
            {
                return bad();
            }
            manifest.cookerVersion = version;
        }
        else if (tag == "options")
        {
            const auto f = fields(line, 2);
            manifest.options = f.size() == 2 ? std::string(f[1]) : std::string();
        }
        else if (tag == "source")
        {
            const auto f = fields(line, 3);
            u64 key = 0;
            if (f.size() != 3 || !parseHex(f[1], key) || !isSafePath(f[2]))
            {
                return bad();
            }
            current = &manifest.sources[std::string(f[2])];
            current->key = key;
        }
        else if (tag == "dep" && current)
        {
            const auto f = fields(line, 3);
            u64 hash = 0;
            if (f.size() != 3 || !parseHex(f[1], hash) || !isSafePath(f[2]))
            {
                return bad();
            }
            current->dependencies.push_back({std::string(f[2]), hash});
        }
        else if (tag == "out" && current)
        {
            const auto f = fields(line, 4);
            u64 hash = 0;
            u64 size = 0;
            if (f.size() != 4 || !parseHex(f[1], hash) || !isSafePath(f[3]))
            {
                return bad();
            }
            const auto [end, ec] = std::from_chars(f[2].data(), f[2].data() + f[2].size(), size);
            if (ec != std::errc() || end != f[2].data() + f[2].size())
            {
                return bad();
            }
            current->outputs.push_back({std::string(f[3]), hash, size});
        }
        else
        {
            return bad();
        }
    }
    if (!headerSeen)
    {
        return Error{"empty manifest"};
    }
    return manifest;
}

std::vector<std::string> gltfExternalUris(std::span<const u8> bytes)
{
    std::vector<std::string> uris;
    const std::string_view json = gltfJson(bytes);
    usize pos = 0;
    while ((pos = json.find("\"uri\"", pos)) != std::string_view::npos)
    {
        pos += 5;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
                                     json[pos] == '\r' || json[pos] == ':'))
        {
            ++pos;
        }
        if (pos >= json.size() || json[pos] != '"')
        {
            continue;
        }
        std::string value;
        for (++pos; pos < json.size() && json[pos] != '"'; ++pos)
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                ++pos; // JSON escapes in URIs are rare; "\/" is the common one
            }
            value += json[pos];
        }
        if (!value.starts_with("data:"))
        {
            uris.push_back(percentDecode(value));
        }
    }
    return uris;
}
} // namespace g7::cook

#include <g7/core/FileSystem.hpp>

#include <cerrno>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace g7::fs
{
namespace
{
BaseDirectories g_baseDirectories;

Error makeError(std::string_view what, const Path& path, const std::error_code& ec)
{
    return Error{std::format("{} '{}': {}", what, toUtf8(path), ec.message())};
}

Error makeErrnoError(std::string_view what, const Path& path)
{
    return makeError(what, path, std::error_code(errno, std::generic_category()));
}

template <typename Container>
Result<Container> readAll(const Path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return makeErrnoError("cannot open file", path);
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        return makeError("cannot query size of", path, ec);
    }
    Container data(static_cast<usize>(size), typename Container::value_type{});
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
    {
        return makeErrnoError("cannot read file", path);
    }
    return data;
}

Result<void> writeAll(const Path& path, const char* data, usize size)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return makeErrnoError("cannot open file for writing", path);
    }
    if (size > 0)
    {
        out.write(data, static_cast<std::streamsize>(size));
    }
    out.flush();
    if (!out)
    {
        return makeErrnoError("cannot write file", path);
    }
    return {};
}

Result<void> writeAllAtomic(const Path& path, const char* data, usize size)
{
    Path tempPath = path;
    tempPath += ".tmp";
    if (auto result = writeAll(tempPath, data, size); !result)
    {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return result;
    }
    // std::filesystem::rename replaces an existing target on all supported platforms.
    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec)
    {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return makeError("cannot replace file", path, ec);
    }
    return {};
}

Path resolve(const Path& base, std::string_view relativeUtf8)
{
    return (base / fromUtf8(relativeUtf8)).lexically_normal();
}
} // namespace

Path fromUtf8(std::string_view utf8)
{
    return Path(std::u8string(utf8.begin(), utf8.end()));
}

std::string toUtf8(const Path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

Result<std::vector<u8>> readFile(const Path& path)
{
    return readAll<std::vector<u8>>(path);
}

Result<std::string> readText(const Path& path)
{
    return readAll<std::string>(path);
}

Result<void> writeFile(const Path& path, std::span<const u8> data)
{
    return writeAll(path, reinterpret_cast<const char*>(data.data()), data.size());
}

Result<void> writeText(const Path& path, std::string_view text)
{
    return writeAll(path, text.data(), text.size());
}

Result<void> writeFileAtomic(const Path& path, std::span<const u8> data)
{
    return writeAllAtomic(path, reinterpret_cast<const char*>(data.data()), data.size());
}

Result<void> writeTextAtomic(const Path& path, std::string_view text)
{
    return writeAllAtomic(path, text.data(), text.size());
}

Result<void> createDirectories(const Path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec)
    {
        return makeError("cannot create directory", path, ec);
    }
    return {};
}

bool exists(const Path& path) noexcept
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

void setBaseDirectories(BaseDirectories dirs)
{
    g_baseDirectories = std::move(dirs);
}

const BaseDirectories& baseDirectories() noexcept
{
    return g_baseDirectories;
}

Path gamePath(std::string_view relativeUtf8)
{
    return resolve(g_baseDirectories.gameDir, relativeUtf8);
}

Path userPath(std::string_view relativeUtf8)
{
    return resolve(g_baseDirectories.userDir, relativeUtf8);
}
} // namespace g7::fs

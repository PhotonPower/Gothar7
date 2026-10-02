#include "PakArchive.hpp"

#include <g7/asset/Vfs.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>

#include <algorithm>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <variant>

namespace g7::asset
{
Result<std::string> normalizeVfsPath(std::string_view path)
{
    std::string out;
    out.reserve(path.size());
    usize start = 0;
    while (start <= path.size())
    {
        usize end = start;
        while (end < path.size() && path[end] != '/' && path[end] != '\\')
        {
            ++end;
        }
        const std::string_view segment = path.substr(start, end - start);
        start = end + 1;
        if (segment.empty() || segment == ".")
        {
            continue;
        }
        if (segment == "..")
        {
            return Error{"invalid VFS path '" + std::string(path) + "': '..' is not allowed"};
        }
        for (const char c : segment)
        {
            if (c == ':' || static_cast<unsigned char>(c) < 0x20)
            {
                return Error{"invalid VFS path '" + std::string(path) + "': forbidden character"};
            }
        }
        if (!out.empty())
        {
            out += '/';
        }
        out += segment;
    }
    if (out.empty())
    {
        return Error{"invalid VFS path '" + std::string(path) + "': empty"};
    }
    return out;
}

namespace
{
struct DiskFile
{
    fs::Path location;
};
struct PakFile
{
    detail::PakEntry entry;
};

/// A file as provided by one mount.
struct FileRef
{
    std::string path; // full VFS path (mount point + relative), original spelling
    u64 size = 0;
    std::variant<DiskFile, PakFile> source;
};

enum class MountKind : u8
{
    Directory,
    Pak
};

struct Mount
{
    MountId id = 0;
    i32 priority = 0;
    u64 order = 0; // mount sequence number; later mounts win on equal priority
    MountKind kind = MountKind::Directory;
    fs::Path source;
    std::string mountPoint; // normalised, may be empty
    std::shared_ptr<const detail::PakArchive> pak;
    std::unordered_map<std::string, FileRef> files; // key: lower-case VFS path
};

std::string joinMountPoint(const std::string& mountPoint, std::string_view relative)
{
    return mountPoint.empty() ? std::string(relative) : mountPoint + "/" + std::string(relative);
}

Result<void> indexDirectory(Mount& m)
{
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        m.source, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        return Error{fs::toUtf8(m.source) + ": cannot read directory (" + ec.message() + ")"};
    }
    // Sorted traversal keeps the choice between case-variant names (Linux) deterministic.
    std::map<std::string, fs::Path> found;
    for (const auto end = std::filesystem::recursive_directory_iterator(); it != end; it.increment(ec))
    {
        if (ec)
        {
            return Error{fs::toUtf8(m.source) + ": cannot read directory (" + ec.message() + ")"};
        }
        if (it->is_regular_file(ec))
        {
            found.emplace(fs::toUtf8(it->path().lexically_relative(m.source)), it->path());
        }
    }
    for (auto& [relative, location] : found)
    {
        auto normalised = normalizeVfsPath(relative);
        if (!normalised)
        {
            G7_LOG_WARN("asset", "{}: skipped file with unusable name '{}'", fs::toUtf8(m.source), relative);
            continue;
        }
        std::string path = joinMountPoint(m.mountPoint, normalised.value());
        std::string key = toLower(path);
        if (m.files.contains(key))
        {
            G7_LOG_WARN("asset", "{}: '{}' differs from another file only in case, ignored",
                        fs::toUtf8(m.source), relative);
            continue;
        }
        const u64 size = std::filesystem::file_size(location, ec);
        m.files.emplace(std::move(key), FileRef{std::move(path), ec ? 0 : size, DiskFile{location}});
    }
    return {};
}

Result<void> indexPak(Mount& m)
{
    auto archive = detail::PakArchive::open(m.source);
    if (!archive)
    {
        return archive.error();
    }
    m.pak = std::make_shared<const detail::PakArchive>(std::move(archive).value());
    for (const detail::PakEntry& e : m.pak->entries())
    {
        std::string path = joinMountPoint(m.mountPoint, e.path);
        std::string key = toLower(path);
        m.files.emplace(std::move(key), FileRef{std::move(path), e.size, PakFile{e}});
    }
    return {};
}

Result<void> buildIndex(Mount& m)
{
    m.files.clear();
    m.pak.reset();
    return m.kind == MountKind::Directory ? indexDirectory(m) : indexPak(m);
}

/// Lower-case key of a lookup path; nullopt for invalid paths (they exist nowhere).
std::optional<std::string> lookupKey(std::string_view path)
{
    auto normalised = normalizeVfsPath(path);
    return normalised ? std::optional<std::string>(toLower(normalised.value())) : std::nullopt;
}

std::string normaliseExtension(std::string_view extension)
{
    std::string ext = toLower(extension);
    if (!ext.empty() && ext.front() != '.')
    {
        ext.insert(ext.begin(), '.');
    }
    return ext;
}
} // namespace

struct Vfs::Impl
{
    mutable std::shared_mutex mutex;
    std::vector<Mount> mounts; // sorted: highest priority first, later mount first on ties
    MountId nextId = 1;
    u64 nextOrder = 0;

    void sortMounts()
    {
        std::sort(mounts.begin(), mounts.end(), [](const Mount& a, const Mount& b)
                  { return a.priority != b.priority ? a.priority > b.priority : a.order > b.order; });
    }

    struct Hit
    {
        const FileRef* file = nullptr;
        const Mount* mount = nullptr;
    };

    /// Winning file for a key and the mount providing it; caller holds the lock.
    [[nodiscard]] Hit find(const std::string& key) const
    {
        for (const Mount& m : mounts)
        {
            if (auto it = m.files.find(key); it != m.files.end())
            {
                return {&it->second, &m};
            }
        }
        return {};
    }

    [[nodiscard]] Mount* byId(MountId id)
    {
        auto it = std::find_if(mounts.begin(), mounts.end(), [id](const Mount& m) { return m.id == id; });
        return it == mounts.end() ? nullptr : &*it;
    }
};

Vfs::Vfs() : m_impl(std::make_unique<Impl>())
{
}
Vfs::~Vfs() = default;
Vfs::Vfs(Vfs&&) noexcept = default;
Vfs& Vfs::operator=(Vfs&&) noexcept = default;

Result<MountId> Vfs::mount(const fs::Path& source, i32 priority, std::string_view mountPoint)
{
    Mount m;
    m.priority = priority;
    m.source = source;
    if (!mountPoint.empty())
    {
        auto normalised = normalizeVfsPath(mountPoint);
        if (!normalised)
        {
            return normalised.error();
        }
        m.mountPoint = std::move(normalised).value();
    }
    std::error_code ec;
    const auto status = std::filesystem::status(source, ec);
    if (ec || !std::filesystem::exists(status))
    {
        return Error{fs::toUtf8(source) + ": mount source not found"};
    }
    m.kind = std::filesystem::is_directory(status) ? MountKind::Directory : MountKind::Pak;
    if (auto indexed = buildIndex(m); !indexed)
    {
        return indexed.error();
    }

    std::unique_lock lock(m_impl->mutex);
    m.id = m_impl->nextId++;
    m.order = m_impl->nextOrder++;
    const MountId id = m.id;
    G7_LOG_INFO("asset", "mounted {} '{}' ({} files, priority {}{}{})",
                m.kind == MountKind::Directory ? "directory" : "archive", fs::toUtf8(source), m.files.size(),
                priority, m.mountPoint.empty() ? "" : ", at ", m.mountPoint);
    m_impl->mounts.push_back(std::move(m));
    m_impl->sortMounts();
    return id;
}

bool Vfs::unmount(MountId id)
{
    std::unique_lock lock(m_impl->mutex);
    const auto before = m_impl->mounts.size();
    std::erase_if(m_impl->mounts, [id](const Mount& m) { return m.id == id; });
    return m_impl->mounts.size() != before;
}

Result<void> Vfs::rescan(MountId id)
{
    std::unique_lock lock(m_impl->mutex);
    Mount* m = m_impl->byId(id);
    if (!m)
    {
        return Error{"unknown mount id " + std::to_string(id)};
    }
    return buildIndex(*m);
}

Result<std::vector<u8>> Vfs::read(std::string_view path) const
{
    const auto key = lookupKey(path);
    if (!key)
    {
        return normalizeVfsPath(path).error();
    }
    FileRef ref;
    std::shared_ptr<const detail::PakArchive> pak;
    {
        std::shared_lock lock(m_impl->mutex);
        const auto hit = m_impl->find(*key);
        if (!hit.file)
        {
            return Error{"file not found in VFS: " + std::string(path)};
        }
        ref = *hit.file;
        pak = hit.mount->pak; // keeps the archive alive if it is unmounted while reading
    }
    // I/O happens outside the lock.
    if (const auto* disk = std::get_if<DiskFile>(&ref.source))
    {
        return fs::readFile(disk->location);
    }
    return pak->read(std::get<PakFile>(ref.source).entry);
}

bool Vfs::exists(std::string_view path) const
{
    return stat(path).has_value();
}

std::optional<VfsFileInfo> Vfs::stat(std::string_view path) const
{
    const auto key = lookupKey(path);
    if (!key)
    {
        return std::nullopt;
    }
    std::shared_lock lock(m_impl->mutex);
    const FileRef* found = m_impl->find(*key).file;
    return found ? std::optional<VfsFileInfo>(VfsFileInfo{found->path, found->size}) : std::nullopt;
}

std::vector<VfsFileInfo> Vfs::list(std::string_view directory, std::string_view extension) const
{
    std::string prefix;
    if (!directory.empty())
    {
        const auto key = lookupKey(directory);
        if (!key)
        {
            return {};
        }
        prefix = *key + "/";
    }
    const std::string ext = normaliseExtension(extension);

    std::map<std::string, VfsFileInfo> result; // key-sorted; first (= winning) mount per key
    std::shared_lock lock(m_impl->mutex);
    for (const Mount& m : m_impl->mounts)
    {
        for (const auto& [key, ref] : m.files)
        {
            if (!key.starts_with(prefix) || (!ext.empty() && !key.ends_with(ext)))
            {
                continue;
            }
            result.try_emplace(key, VfsFileInfo{ref.path, ref.size});
        }
    }
    std::vector<VfsFileInfo> out;
    out.reserve(result.size());
    for (auto& [key, info] : result)
    {
        out.push_back(std::move(info));
    }
    return out;
}

std::optional<fs::Path> Vfs::diskPath(std::string_view path) const
{
    const auto key = lookupKey(path);
    if (!key)
    {
        return std::nullopt;
    }
    std::shared_lock lock(m_impl->mutex);
    const FileRef* found = m_impl->find(*key).file;
    if (!found)
    {
        return std::nullopt;
    }
    const auto* disk = std::get_if<DiskFile>(&found->source);
    return disk ? std::optional<fs::Path>(disk->location) : std::nullopt;
}

usize Vfs::mountCount() const
{
    std::shared_lock lock(m_impl->mutex);
    return m_impl->mounts.size();
}
} // namespace g7::asset

#pragma once

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// Identifies a mount for unmount()/rescan(); 0 is never used.
using MountId = u32;

/// A file visible in the VFS. ``path`` is normalised (see normalizeVfsPath) and keeps the
/// spelling of the source that provides it.
struct VfsFileInfo
{
    std::string path;
    u64 size = 0;
};

/// Normalises a VFS path: '\' becomes '/', empty and "." segments are dropped, a leading '/' is
/// ignored. Rejects "..", ':' (drive letters), control characters and empty results. Spelling is
/// kept; lookups compare case-insensitively.
[[nodiscard]] Result<std::string> normalizeVfsPath(std::string_view path);

/// Virtual file system over directories and .g7pak archives (docs/modules/asset.md).
///
/// Every mount provides files under an optional mount point. When several mounts provide the same
/// path, the one with the higher priority wins; on equal priority the later mount wins - mods and
/// patches overlay the base data like Gothic's VDF archives. Paths are case-insensitive on every
/// platform: directories are indexed when mounted (rescan() picks up later changes).
///
/// Thread safety: read(), exists(), stat(), list() and diskPath() may run concurrently (asset
/// worker threads); mount(), unmount() and rescan() take an exclusive lock.
class Vfs
{
public:
    Vfs();
    ~Vfs();
    Vfs(Vfs&&) noexcept;
    Vfs& operator=(Vfs&&) noexcept;
    Vfs(const Vfs&) = delete;
    Vfs& operator=(const Vfs&) = delete;

    /// Mounts a directory or a .g7pak archive (detected by the file type).
    [[nodiscard]] Result<MountId> mount(const fs::Path& source, i32 priority,
                                        std::string_view mountPoint = {});
    /// Returns false if the id is unknown.
    bool unmount(MountId id);
    /// Re-reads a mount's index (new or removed loose files, rewritten archive).
    [[nodiscard]] Result<void> rescan(MountId id);

    [[nodiscard]] Result<std::vector<u8>> read(std::string_view path) const;
    [[nodiscard]] bool exists(std::string_view path) const;
    [[nodiscard]] std::optional<VfsFileInfo> stat(std::string_view path) const;
    /// All visible files below ``directory`` (recursive, "" = everything), optionally only those
    /// ending in ``extension`` (with or without dot, case-insensitive); sorted case-insensitively.
    [[nodiscard]] std::vector<VfsFileInfo> list(std::string_view directory = {},
                                                std::string_view extension = {}) const;
    /// Location on disk if the winning source is a loose file (for hot reload), else nullopt.
    [[nodiscard]] std::optional<fs::Path> diskPath(std::string_view path) const;

    [[nodiscard]] usize mountCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::asset

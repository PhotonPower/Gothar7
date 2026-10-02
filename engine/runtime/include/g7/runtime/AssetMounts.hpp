#pragma once

// Which directories and archives the engine mounts into its VFS ([assets] in engine.toml).

#include <g7/core/Config.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace g7
{
struct MountSpec
{
    fs::Path source; ///< directory or .g7pak archive
    i32 priority = 0;
    std::string mountPoint;
};

/// Development mounts of the repository's assets folder: `source` (priority 0) and, if present,
/// `cooked` (priority 10, so cooked data wins over its sources).
inline constexpr i32 kDevSourcePriority = 0;
inline constexpr i32 kDevCookedPriority = 10;
/// Archives in assets/cooked (e.g. data.g7pak from g7-cook --pack): above the loose cooked files.
inline constexpr i32 kDevCookedArchivePriority = 11;

/// Reads the mount list:
/// - `[assets] dev_mounts = true` (default) adds `<devRoot>/source`, `<devRoot>/cooked` and every
///   `<devRoot>/cooked/*.g7pak` archive when `devRoot` is set (development builds, G7_DEV_ASSETS)
///   and the folders exist;
/// - every `[[assets.mount]]` with `path` (relative paths start at `gameDir`), `priority` (default 0)
///   and `mount_point` (default root). A path ending in `*.g7pak` mounts every archive of that
///   folder in name order, so on equal priority later names win (patch_02 over patch_01).
/// Missing folders are left out (the caller logs them); a malformed entry is an error.
[[nodiscard]] Result<std::vector<MountSpec>> assetMounts(const Config& settings, const fs::Path& gameDir,
                                                         const fs::Path& devRoot);

/// Turns an image URI of a mesh into the VFS paths to try, in order: the URI as a path from the
/// VFS root (cooked .g7mesh files store those), then relative to the mesh's folder (glTF).
[[nodiscard]] std::vector<std::string> imageCandidates(std::string_view meshPath, std::string_view uri);
/// Path of `relative` next to the VFS file `base` ("scenes/a.toml" + "models/b.glb").
[[nodiscard]] std::string vfsSibling(std::string_view base, std::string_view relative);
} // namespace g7

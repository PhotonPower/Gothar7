#pragma once

// g7-cook: converts source assets into runtime formats and optionally packs them (ADR 0016,
// docs/06-asset-pipeline.md). Version 1: glTF/GLB -> .g7mesh, images unchanged (KTX2 follows),
// everything else copied.

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <string>
#include <vector>

namespace g7::cook
{
struct CookOptions
{
    fs::Path source; ///< e.g. assets/source
    fs::Path out;    ///< e.g. assets/cooked
    /// Archive file name (relative to `out`, e.g. "data.g7pak"); empty = write loose files.
    std::string pack;
    /// Delete `out` before cooking (stale outputs of removed sources).
    bool clean = false;
    /// zstd level for archive entries (1 = fast for development .. 22 = smallest).
    int level = 19;
};

struct CookReport
{
    u32 meshes = 0;                  ///< glTF/GLB cooked to .g7mesh
    u32 images = 0;                  ///< image files plus images extracted from meshes
    u32 copied = 0;                  ///< other files taken over unchanged
    u32 skipped = 0;                 ///< hidden files, Blender files, glTF buffers (.bin)
    u32 outputs = 0;                 ///< files written (or packed)
    std::vector<std::string> errors; ///< per-file problems; the rest is still cooked
};

/// Cooks every file below `source`. Fatal problems (missing source, `out` inside `source`,
/// unwritable output) are returned as error; problems with single files end up in
/// CookReport::errors. Output is deterministic: same sources, same bytes.
[[nodiscard]] Result<CookReport> cook(const CookOptions& options);
} // namespace g7::cook

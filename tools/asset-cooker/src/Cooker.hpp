#pragma once

// g7-cook: converts source assets into runtime formats and optionally packs them (ADR 0016,
// docs/06-asset-pipeline.md): glTF/GLB -> .g7mesh, images unchanged or KTX2, everything else copied.

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <string>
#include <vector>

namespace g7::cook
{
enum class TextureMode : u8
{
    Copy, ///< images unchanged (library default; builds without libktx, development)
    Ktx2, ///< images -> .ktx2 (UASTC + zstd, mips); normal maps detected from mesh materials (g7-cook
          ///< default)
};

struct CookOptions
{
    fs::Path source; ///< e.g. assets/source
    fs::Path out;    ///< e.g. assets/cooked
    /// Archive file name (relative to `out`, e.g. "data.g7pak"); empty = write loose files.
    std::string pack;
    /// Delete `out` before cooking (normally not needed: the manifest removes outputs of deleted sources).
    bool clean = false;
    /// Ignore the manifest and cook everything (the manifest is rewritten).
    bool full = false;
    /// zstd level for archive entries (1 = fast for development .. 22 = smallest).
    int level = 19;
    TextureMode textures = TextureMode::Copy;
    /// UASTC quality for TextureMode::Ktx2: 0 = fastest .. 4 = best (2 = default).
    u32 uastcLevel = 2;
};

struct CookReport
{
    u32 meshes = 0;                  ///< glTF/GLB cooked to .g7mesh
    u32 images = 0;                  ///< image files plus images extracted from meshes
    u32 copied = 0;                  ///< other files taken over unchanged
    u32 skipped = 0;                 ///< hidden files, Blender files, glTF buffers (.bin)
    u32 outputs = 0;                 ///< files in the output (written, unchanged or packed)
    u32 cooked = 0;                  ///< sources cooked this time
    u32 reused = 0;                  ///< sources whose previous outputs were reused (manifest)
    std::vector<std::string> errors; ///< per-file problems; the rest is still cooked
};

/// Cooks every file below `source`. Fatal problems (missing source, `out` inside `source`,
/// unwritable output) are returned as error; problems with single files end up in
/// CookReport::errors. Output is deterministic: same sources, same bytes.
///
/// Incremental: `<out>/.g7cook/manifest.txt` records per source a key (bytes, dependencies,
/// cooker version, options) and its outputs. Sources with an unchanged key reuse their previous
/// outputs; outputs of deleted sources are removed (only files listed in the manifest).
[[nodiscard]] Result<CookReport> cook(const CookOptions& options);
} // namespace g7::cook

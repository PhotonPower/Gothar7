#pragma once

// Cook manifest for incremental cooking (docs/06-asset-pipeline.md): per source a key over its
// bytes, dependencies, cooker version and options, plus the outputs it produced.

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::cook
{
/// Bump whenever the cooker's output for the same input changes (format, encoder settings).
inline constexpr u32 kCookerVersion = 1;
/// Manifest location below the output directory.
inline constexpr std::string_view kManifestPath = ".g7cook/manifest.txt";

/// 64-bit FNV-1a; `seed` chains several inputs into one hash.
[[nodiscard]] u64 hashBytes(std::span<const u8> data, u64 seed = 0xcbf29ce484222325ull) noexcept;
[[nodiscard]] u64 hashText(std::string_view text, u64 seed = 0xcbf29ce484222325ull) noexcept;

struct ManifestDependency
{
    std::string path; ///< VFS path below the source directory
    u64 hash = 0;     ///< 0 = missing
};

struct ManifestOutput
{
    std::string path;
    u64 hash = 0;
    u64 size = 0;
};

struct ManifestEntry
{
    u64 key = 0;
    std::vector<ManifestDependency> dependencies;
    std::vector<ManifestOutput> outputs;
};

struct Manifest
{
    u32 cookerVersion = kCookerVersion;
    std::string options; ///< output-relevant options, e.g. "textures=ktx2 uastc=2 output=pack:data.g7pak"
    std::map<std::string, ManifestEntry> sources; ///< by source path (sorted)
};

/// Tab-separated text, one record per line, sorted:
///
///     g7cook-manifest<TAB>1
///     cooker<TAB><version>
///     options<TAB><options>
///     source<TAB><key hex><TAB><path>
///     dep<TAB><hash hex><TAB><path>
///     out<TAB><hash hex><TAB><size><TAB><path>
///
/// Paths come last, so they may contain spaces (VFS paths contain no tabs or control characters).
[[nodiscard]] std::string serializeManifest(const Manifest& manifest);
[[nodiscard]] Result<Manifest> parseManifest(std::string_view text);

/// Non-data URIs referenced by a glTF (.gltf JSON or the JSON chunk of a .glb): buffers and images,
/// percent-decoded, in file order.
[[nodiscard]] std::vector<std::string> gltfExternalUris(std::span<const u8> bytes);
} // namespace g7::cook

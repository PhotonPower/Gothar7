#include <g7/asset/Vfs.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/runtime/AssetMounts.hpp>

#include <algorithm>
#include <filesystem>
#include <format>

namespace g7
{
namespace
{
bool isDirectory(const fs::Path& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

/// All .g7pak files directly in `directory`, sorted by name.
std::vector<fs::Path> archivesIn(const fs::Path& directory)
{
    std::vector<fs::Path> archives;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
    {
        if (entry.is_regular_file(ec) && equalsIgnoreCase(fs::toUtf8(entry.path().extension()), ".g7pak"))
        {
            archives.push_back(entry.path());
        }
    }
    std::sort(archives.begin(), archives.end());
    return archives;
}
} // namespace

Result<std::vector<MountSpec>> assetMounts(const Config& settings, const fs::Path& gameDir,
                                           const fs::Path& devRoot)
{
    std::vector<MountSpec> mounts;
    if (settings.get<bool>("assets.dev_mounts", true) && !devRoot.empty())
    {
        for (const auto& [folder, priority] :
             {std::pair{"source", kDevSourcePriority}, std::pair{"cooked", kDevCookedPriority}})
        {
            if (const fs::Path path = (devRoot / folder).make_preferred(); isDirectory(path))
            {
                mounts.push_back({path, priority, {}});
            }
        }
        // A folder mount shows an archive only as a file: mount the cooked archives themselves too.
        for (const fs::Path& archive : archivesIn((devRoot / "cooked").make_preferred()))
        {
            mounts.push_back({archive.lexically_normal(), kDevCookedArchivePriority, {}});
        }
    }

    for (usize i = 0, n = settings.arraySize("assets.mount"); i < n; ++i)
    {
        const std::string prefix = std::format("assets.mount[{}]", i);
        const auto path = settings.find<std::string>(prefix + ".path");
        if (!path || path->empty())
        {
            return Error{std::format("[assets] mount {} needs a 'path'", i)};
        }
        if (settings.contains(prefix + ".priority") && !settings.find<i64>(prefix + ".priority"))
        {
            return Error{std::format("[assets] mount {}: 'priority' must be an integer", i)};
        }
        const auto priority = static_cast<i32>(settings.get<i64>(prefix + ".priority", 0));
        const std::string mountPoint = settings.get<std::string>(prefix + ".mount_point", "");

        std::string pattern = *path;
        const bool archives = pattern.ends_with("*.g7pak");
        if (archives)
        {
            pattern.resize(pattern.size() - std::string_view("*.g7pak").size());
        }
        fs::Path source = fs::fromUtf8(pattern.empty() ? "." : pattern);
        if (source.is_relative())
        {
            source = gameDir / source;
        }
        if (!archives)
        {
            mounts.push_back({source.lexically_normal(), priority, mountPoint});
            continue;
        }
        for (const fs::Path& archive : archivesIn(source))
        {
            mounts.push_back({archive.lexically_normal(), priority, mountPoint});
        }
    }
    return mounts;
}

std::string vfsSibling(std::string_view base, std::string_view relative)
{
    // Directory of `base` plus `relative`, with "." and ".." resolved lexically; ".." above the root
    // is kept, so the VFS rejects the result instead of silently reading a different file.
    std::vector<std::string_view> parts;
    const auto split = [&](std::string_view text)
    {
        usize start = 0;
        while (start <= text.size())
        {
            const usize end = std::min(text.find_first_of("/\\", start), text.size());
            const std::string_view part = text.substr(start, end - start);
            if (part == "..")
            {
                if (!parts.empty() && parts.back() != "..")
                {
                    parts.pop_back();
                }
                else
                {
                    parts.push_back(part);
                }
            }
            else if (!part.empty() && part != ".")
            {
                parts.push_back(part);
            }
            start = end + 1;
        }
    };
    const usize slash = base.find_last_of("/\\");
    split(slash == std::string_view::npos ? std::string_view{} : base.substr(0, slash));
    split(relative);
    std::string result;
    for (const std::string_view part : parts)
    {
        if (!result.empty())
        {
            result += '/';
        }
        result += part;
    }
    return result;
}

std::string preferCooked(const asset::Vfs& vfs, std::string_view path)
{
    for (const std::string_view extension : {std::string_view(".glb"), std::string_view(".gltf")})
    {
        if (path.size() > extension.size() &&
            equalsIgnoreCase(path.substr(path.size() - extension.size()), extension))
        {
            std::string cooked = std::string(path.substr(0, path.size() - extension.size())) + ".g7mesh";
            if (vfs.exists(cooked))
            {
                return cooked;
            }
        }
    }
    return std::string(path);
}

std::vector<std::string> imageCandidates(std::string_view meshPath, std::string_view uri)
{
    std::vector<std::string> candidates{std::string(uri)};
    std::string relative = vfsSibling(meshPath, uri);
    if (relative != candidates.front())
    {
        candidates.push_back(std::move(relative));
    }
    return candidates;
}
} // namespace g7

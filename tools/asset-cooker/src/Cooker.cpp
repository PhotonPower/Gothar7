#include "Cooker.hpp"

#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/asset/MeshFile.hpp>
#include <g7/asset/Pak.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>

#include <algorithm>
#include <filesystem>
#include <map>

namespace g7::cook
{
namespace
{
constexpr std::string_view kMeshExtensions[] = {".glb", ".gltf"};
constexpr std::string_view kImageExtensions[] = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
/// Not game data: Blender sources and their backups, glTF buffers (read through the .gltf).
constexpr std::string_view kSkippedExtensions[] = {".blend", ".blend1", ".bin"};

bool contains(std::span<const std::string_view> list, std::string_view value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

std::string extensionOf(std::string_view path)
{
    const auto slash = path.rfind('/');
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
    {
        return {};
    }
    return toLower(path.substr(dot));
}

std::string withoutExtension(std::string_view path)
{
    const auto ext = extensionOf(path);
    return std::string(path.substr(0, path.size() - ext.size()));
}

std::string directoryOf(std::string_view path)
{
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

bool isHidden(std::string_view relative)
{
    return relative.starts_with('.') || relative.find("/.") != std::string_view::npos;
}

std::string imageExtension(const asset::ImageSource& image)
{
    if (image.mimeType == "image/png")
    {
        return ".png";
    }
    if (image.mimeType == "image/jpeg")
    {
        return ".jpg";
    }
    const auto& b = image.encoded;
    if (b.size() >= 4 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
    {
        return ".png";
    }
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xD8)
    {
        return ".jpg";
    }
    return {};
}

bool isWithin(const fs::Path& inner, const fs::Path& outer)
{
    const auto rel = inner.lexically_relative(outer);
    return !rel.empty() && *rel.begin() != "..";
}

class Cook
{
public:
    explicit Cook(const CookOptions& options) : m_options(options) {}

    void run()
    {
        for (const auto& [relative, location] : collectSources())
        {
            cookFile(relative, location);
        }
    }

    [[nodiscard]] Result<void> write()
    {
        if (!m_options.pack.empty())
        {
            asset::PakWriter pak;
            pak.setCompressionLevel(m_options.level);
            for (const auto& [path, data] : m_outputs)
            {
                if (auto added = pak.add(path, data); !added)
                {
                    return added.error();
                }
            }
            const fs::Path target = m_options.out / fs::fromUtf8(m_options.pack);
            if (auto created = fs::createDirectories(target.parent_path()); !created)
            {
                return created.error();
            }
            m_report.outputs = static_cast<u32>(m_outputs.size());
            return pak.write(target);
        }
        for (const auto& [path, data] : m_outputs)
        {
            const fs::Path target = m_options.out / fs::fromUtf8(path);
            if (auto created = fs::createDirectories(target.parent_path()); !created)
            {
                return created.error();
            }
            if (auto written = fs::writeFileAtomic(target, data); !written)
            {
                return written.error();
            }
            ++m_report.outputs;
        }
        return {};
    }

    [[nodiscard]] CookReport report() && { return std::move(m_report); }

private:
    /// Regular files below the source, keyed by normalised relative path (sorted, deterministic).
    std::map<std::string, fs::Path> collectSources()
    {
        std::map<std::string, fs::Path> files;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(m_options.source, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (!it->is_regular_file(ec))
            {
                continue;
            }
            const std::string raw = fs::toUtf8(it->path().lexically_relative(m_options.source));
            auto normalised = asset::normalizeVfsPath(raw);
            if (!normalised)
            {
                fail(raw, normalised.error().message);
                continue;
            }
            files.emplace(std::move(normalised).value(), it->path());
        }
        if (ec)
        {
            fail(fs::toUtf8(m_options.source), "cannot read directory (" + ec.message() + ")");
        }
        return files;
    }

    void cookFile(const std::string& relative, const fs::Path& location)
    {
        const std::string ext = extensionOf(relative);
        if (isHidden(relative) || contains(kSkippedExtensions, ext))
        {
            ++m_report.skipped;
            return;
        }
        if (contains(kMeshExtensions, ext))
        {
            cookMesh(relative, location);
            return;
        }
        auto bytes = fs::readFile(location);
        if (!bytes)
        {
            fail(relative, bytes.error().message);
            return;
        }
        const bool image = contains(kImageExtensions, ext);
        if (image)
        {
            // Version 1 keeps images as they are, but refuses broken ones early.
            if (auto decoded = asset::decodeImage(bytes.value(), relative); !decoded)
            {
                fail(relative, decoded.error().message);
                return;
            }
        }
        if (emit(relative, relative, std::move(bytes).value()))
        {
            ++(image ? m_report.images : m_report.copied);
        }
    }

    void cookMesh(const std::string& relative, const fs::Path& location)
    {
        auto loaded = asset::loadGltf(location);
        if (!loaded)
        {
            fail(relative, loaded.error().message);
            return;
        }
        asset::MeshData mesh = std::move(loaded).value();
        const std::string base = withoutExtension(relative);
        // Extracted images are emitted only if the whole mesh cooks (no orphans on errors).
        std::vector<std::pair<std::string, std::vector<u8>>> extracted;
        for (usize i = 0; i < mesh.images.size(); ++i)
        {
            asset::ImageSource& image = mesh.images[i];
            if (!image.encoded.empty())
            {
                // Embedded image -> own file next to the mesh.
                const std::string ext = imageExtension(image);
                if (ext.empty())
                {
                    fail(relative, "embedded image " + std::to_string(i) + " has an unknown format");
                    return;
                }
                image.uri = base + ".img" + std::to_string(i) + ext;
                extracted.emplace_back(image.uri, std::move(image.encoded));
                image.encoded.clear();
                continue;
            }
            // External image: cooked on its own; the mesh refers to it by its VFS path.
            const fs::Path referenced = (location.parent_path() / fs::fromUtf8(image.uri)).lexically_normal();
            auto vfsPath =
                asset::normalizeVfsPath(fs::toUtf8(referenced.lexically_relative(m_options.source)));
            if (!isWithin(referenced, m_options.source) || !vfsPath)
            {
                fail(relative, "texture '" + image.uri + "' lies outside the source directory");
                return;
            }
            if (!fs::exists(referenced))
            {
                fail(relative, "missing texture '" + image.uri + "'");
                return;
            }
            image.uri = std::move(vfsPath).value();
        }
        const usize images = extracted.size();
        extracted.emplace_back(base + ".g7mesh", asset::serializeMesh(mesh));
        if (emitAll(relative, std::move(extracted)))
        {
            m_report.images += static_cast<u32>(images);
            ++m_report.meshes;
        }
    }

    bool emit(const std::string& source, std::string path, std::vector<u8> data)
    {
        std::vector<std::pair<std::string, std::vector<u8>>> one;
        one.emplace_back(std::move(path), std::move(data));
        return emitAll(source, std::move(one));
    }

    /// Adds all outputs of one source file, or none if any of them collides with an earlier output.
    bool emitAll(const std::string& source, std::vector<std::pair<std::string, std::vector<u8>>> files)
    {
        for (const auto& [path, data] : files)
        {
            if (auto it = m_keys.find(toLower(path)); it != m_keys.end())
            {
                fail(source, "output '" + path + "' collides with '" + it->second +
                                 "' (same name in another spelling or format, e.g. hut.glb and hut.gltf)");
                return false;
            }
        }
        for (auto& [path, data] : files)
        {
            m_keys.emplace(toLower(path), path);
            m_outputs.emplace(std::move(path), std::move(data));
        }
        return true;
    }

    void fail(const std::string& path, const std::string& message)
    {
        G7_LOG_ERROR("cook", "{}: {}", path, message);
        m_report.errors.push_back(path + ": " + message);
    }

    const CookOptions& m_options;
    std::map<std::string, std::vector<u8>> m_outputs; // sorted by path
    std::map<std::string, std::string> m_keys;        // lower-case path -> path (collisions)
    CookReport m_report;
};
} // namespace

Result<CookReport> cook(const CookOptions& options)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(options.source, ec))
    {
        return Error{"source directory not found: " + fs::toUtf8(options.source)};
    }
    const fs::Path source = std::filesystem::weakly_canonical(options.source, ec);
    const fs::Path out = std::filesystem::weakly_canonical(options.out, ec);
    if (source == out || isWithin(out, source))
    {
        return Error{"output directory must not lie inside the source directory"};
    }
    if (options.clean && isWithin(source, out))
    {
        return Error{"--clean would delete the source directory (it lies inside the output)"};
    }
    if (options.clean)
    {
        std::filesystem::remove_all(out, ec);
        if (ec)
        {
            return Error{"cannot clean " + fs::toUtf8(out) + " (" + ec.message() + ")"};
        }
    }

    CookOptions resolved = options;
    resolved.source = source;
    resolved.out = out;
    Cook cook(resolved);
    cook.run();
    if (auto written = cook.write(); !written)
    {
        return written.error();
    }
    CookReport report = std::move(cook).report();
    G7_LOG_INFO("cook", "{} meshes, {} images, {} copied, {} skipped -> {} outputs ({} errors)",
                report.meshes, report.images, report.copied, report.skipped, report.outputs,
                report.errors.size());
    return report;
}
} // namespace g7::cook

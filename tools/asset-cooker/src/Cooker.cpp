#include "Cooker.hpp"

#include "Manifest.hpp"
#include "Textures.hpp"

#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/asset/MeshFile.hpp>
#include <g7/asset/Pak.hpp>
#include <g7/asset/Vfs.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/world/WorldFile.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <thread>

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
    explicit Cook(const CookOptions& options) : m_options(options) { m_new.options = optionsString(); }

    void run()
    {
        loadPreviousManifest();
        const auto sources = collectSources();
        if (m_options.textures == TextureMode::Ktx2)
        {
            scanTextureUsage(sources);
            startKtx2Encoding(sources);
        }
        for (const auto& [relative, location] : sources)
        {
            cookFile(relative, location);
        }
    }

    [[nodiscard]] Result<void> write()
    {
        m_report.outputs = static_cast<u32>(m_outputs.size());
        if (!m_options.pack.empty())
        {
            const bool hadArchive = m_previousPak != nullptr;
            m_previousPak.reset(); // everything reused has been read; the archive gets replaced
            if (hadArchive && nothingChanged())
            {
                return {}; // identical archive: skip recompressing, keep its timestamp (hot reload)
            }
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
            if (auto written = pak.write(target); !written)
            {
                return written;
            }
            return writeManifest();
        }
        for (const auto& [path, data] : m_outputs)
        {
            const fs::Path target = m_options.out / fs::fromUtf8(path);
            // Unchanged files are not rewritten: their timestamps stay, hot reload stays quiet.
            if (m_unchanged.contains(path) || sameContent(target, data))
            {
                continue;
            }
            if (auto created = fs::createDirectories(target.parent_path()); !created)
            {
                return created.error();
            }
            if (auto written = fs::writeFileAtomic(target, data); !written)
            {
                return written.error();
            }
        }
        removeStaleOutputs();
        return writeManifest();
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
        auto bytes = fs::readFile(location);
        if (!bytes)
        {
            fail(relative, bytes.error().message);
            return;
        }
        const bool mesh = contains(kMeshExtensions, ext);
        ManifestEntry entry;
        if (mesh)
        {
            entry.dependencies = meshDependencies(location, bytes.value());
        }
        entry.key = sourceKey(relative, bytes.value(), entry.dependencies, textureUsageTag(relative, ext));

        m_emitted.clear();
        if (reusePrevious(relative, entry))
        {
            return;
        }
        if (mesh)
        {
            cookMesh(relative, location);
        }
        else
        {
            cookBytes(relative, ext, std::move(bytes).value());
        }
        if (!m_emitted.empty()) // empty: the source failed and stays out of the manifest
        {
            entry.outputs = std::move(m_emitted);
            m_new.sources.emplace(relative, std::move(entry));
            ++m_report.cooked;
        }
    }

    void cookBytes(const std::string& relative, const std::string& ext, std::vector<u8> bytes)
    {
        const bool image = contains(kImageExtensions, ext);
        if (image)
        {
            // Broken images are refused early, also when they are only copied.
            auto decoded = asset::decodeImage(bytes, relative);
            if (!decoded)
            {
                fail(relative, decoded.error().message);
                return;
            }
            if (m_options.textures == TextureMode::Ktx2)
            {
                cookKtx2(relative, decoded.value());
                return;
            }
        }
        if (emit(relative, relative, std::move(bytes)))
        {
            ++(image ? m_report.images : m_report.copied);
        }
    }

    void cookKtx2(const std::string& relative, const asset::ImageData& image)
    {
        const bool normal = m_normalImages.contains(relative);
        const bool color = m_colorImages.contains(relative);
        const bool data = m_dataImages.contains(relative);
        if (normal && color)
        {
            fail(relative, "used both as colour texture and as normal map");
            return;
        }
        if (data && (normal || color))
        {
            fail(relative, "used both as terrain splat map (data) and as colour texture or normal map");
            return;
        }
        const auto usage = normal ? TextureUsage::Normal : data ? TextureUsage::Data : TextureUsage::Color;
        // Encoded ahead on the workers (startKtx2Encoding), or here if it was not expected to be cooked.
        const auto ahead = m_ktx2Ahead.find(relative);
        auto ktx =
            ahead != m_ktx2Ahead.end() ? ahead->second.get() : encodeKtx2(image, usage, m_options.uastcLevel);
        if (ahead != m_ktx2Ahead.end())
        {
            m_ktx2Ahead.erase(ahead);
        }
        if (!ktx)
        {
            fail(relative, ktx.error().message);
            return;
        }
        if (emit(relative, withoutExtension(relative) + ".ktx2", std::move(ktx).value()))
        {
            ++m_report.images;
        }
    }

    /// KTX2 encoding (UASTC) is by far the slowest step and basisu runs single-threaded (Textures.cpp):
    /// the images that will be cooked (new key) are encoded in parallel on own workers while the main
    /// thread goes through the sources in order. Order, reports and outputs stay exactly as before.
    void startKtx2Encoding(const std::map<std::string, fs::Path>& sources)
    {
        for (const auto& [relative, location] : sources)
        {
            const std::string ext = extensionOf(relative);
            if (isHidden(relative) || contains(kSkippedExtensions, ext) || !contains(kImageExtensions, ext))
            {
                continue;
            }
            const bool normal = m_normalImages.contains(relative);
            const bool color = m_colorImages.contains(relative);
            const bool data = m_dataImages.contains(relative);
            if ((normal && color) || (data && (normal || color)))
            {
                continue; // an error, reported when the file's turn comes
            }
            auto bytes = fs::readFile(location);
            if (!bytes)
            {
                continue; // reported when the file's turn comes
            }
            const u64 key = sourceKey(relative, bytes.value(), {}, textureUsageTag(relative, ext));
            if (m_previous)
            {
                const auto previous = m_previous->sources.find(relative);
                if (previous != m_previous->sources.end() && previous->second.key == key)
                {
                    continue; // most likely reused (if its outputs changed by hand, cookKtx2 encodes it)
                }
            }
            auto job = std::make_unique<Ktx2Job>();
            job->bytes = std::move(bytes).value();
            job->name = relative;
            job->usage = normal ? TextureUsage::Normal : data ? TextureUsage::Data : TextureUsage::Color;
            m_ktx2Ahead.emplace(relative, job->result.get_future());
            m_ktx2Jobs.push_back(std::move(job));
        }
        if (m_ktx2Jobs.empty())
        {
            return;
        }
        const u32 hardware = std::max(1u, std::thread::hardware_concurrency());
        const auto workers = static_cast<usize>(std::max(1u, hardware - 1)); // the main thread cooks meshes
        for (usize w = 0; w < std::min(workers, m_ktx2Jobs.size()); ++w)
        {
            m_ktx2Workers.emplace_back(
                [this]
                {
                    for (usize i = m_ktx2Next.fetch_add(1); i < m_ktx2Jobs.size();
                         i = m_ktx2Next.fetch_add(1))
                    {
                        Ktx2Job& job = *m_ktx2Jobs[i];
                        auto decoded = asset::decodeImage(job.bytes, job.name);
                        job.result.set_value(
                            decoded ? encodeKtx2(decoded.value(), job.usage, m_options.uastcLevel)
                                    : Result<std::vector<u8>>(decoded.error()));
                        job.bytes = {};
                    }
                });
        }
    }

    void cookMesh(const std::string& relative, const fs::Path& location)
    {
        auto cached = m_meshes.find(relative);
        auto loaded = cached != m_meshes.end() ? std::move(cached->second) : asset::loadGltf(location);
        if (!loaded)
        {
            fail(relative, loaded.error().message);
            return;
        }
        asset::MeshData mesh = std::move(loaded).value();
        if (mesh.submeshes.empty())
        {
            // E.g. an animation set (anims/*.glb) without geometry: nothing to cook until skeletons and
            // clips come with M6 - an empty .g7mesh would only mislead.
            G7_LOG_WARN("cook", "{}: no mesh in this glTF, skipped", relative);
            ++m_report.skipped;
            return;
        }
        const bool ktx2 = m_options.textures == TextureMode::Ktx2;
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
                if (ktx2)
                {
                    auto encoded = encodeEmbedded(mesh, i, m_options.uastcLevel);
                    if (!encoded)
                    {
                        fail(relative,
                             "embedded image " + std::to_string(i) + ": " + encoded.error().message);
                        return;
                    }
                    image.uri = base + ".img" + std::to_string(i) + ".ktx2";
                    image.mimeType = "image/ktx2";
                    extracted.emplace_back(image.uri, std::move(encoded).value());
                }
                else
                {
                    image.uri = base + ".img" + std::to_string(i) + ext;
                    extracted.emplace_back(image.uri, std::move(image.encoded));
                }
                image.encoded.clear();
                continue;
            }
            // External image: cooked on its own; the mesh refers to it by its VFS path.
            auto vfsPath = resolveTexture(location, image.uri);
            if (!vfsPath)
            {
                fail(relative, vfsPath.error().message);
                return;
            }
            image.uri = std::move(vfsPath).value();
            if (ktx2 && contains(kImageExtensions, extensionOf(image.uri)))
            {
                image.uri = withoutExtension(image.uri) + ".ktx2";
                image.mimeType = "image/ktx2";
            }
        }
        const usize images = extracted.size();
        extracted.emplace_back(base + ".g7mesh", asset::serializeMesh(mesh));
        if (emitAll(relative, std::move(extracted)))
        {
            m_report.images += static_cast<u32>(images);
            ++m_report.meshes;
        }
    }

    /// VFS path of a texture referenced by a mesh (relative to the mesh file); must exist in the source.
    Result<std::string> resolveTexture(const fs::Path& meshLocation, const std::string& uri) const
    {
        const fs::Path referenced = (meshLocation.parent_path() / fs::fromUtf8(uri)).lexically_normal();
        auto vfsPath = asset::normalizeVfsPath(fs::toUtf8(referenced.lexically_relative(m_options.source)));
        if (!isWithin(referenced, m_options.source) || !vfsPath)
        {
            return Error{"texture '" + uri + "' lies outside the source directory"};
        }
        if (!fs::exists(referenced))
        {
            return Error{"missing texture '" + uri + "'"};
        }
        return vfsPath;
    }

    /// KTX2 needs to know before encoding how an image is used: collect how the meshes use their
    /// external textures (and keep the parsed meshes for cookMesh) and what the terrain blocks of
    /// worlds name (splat maps are data, layer albedos colour, layer normals normal maps).
    void scanTextureUsage(const std::map<std::string, fs::Path>& sources)
    {
        for (const auto& [relative, location] : sources)
        {
            if (!isHidden(relative) && extensionOf(relative) == ".g7world")
            {
                scanWorld(relative, location);
                continue;
            }
            if (isHidden(relative) || !contains(kMeshExtensions, extensionOf(relative)))
            {
                continue;
            }
            auto loaded = asset::loadGltf(location);
            if (loaded)
            {
                const asset::MeshData& mesh = loaded.value();
                for (const asset::MaterialInfo& m : mesh.materials)
                {
                    noteUsage(mesh, location, m.normalImage, m_normalImages);
                    noteUsage(mesh, location, m.baseColorImage, m_colorImages);
                    noteUsage(mesh, location, m.emissiveImage, m_colorImages);
                }
            }
            m_meshes.emplace(relative, std::move(loaded));
        }
    }

    void scanWorld(const std::string& relative, const fs::Path& location)
    {
        auto text = fs::readText(location);
        auto world =
            text ? world::parseWorldFile(text.value(), relative) : Result<world::WorldFile>(text.error());
        if (!world)
        {
            // Copied anyway; the engine reports the same problem when it loads the world.
            G7_LOG_WARN("cook", "{}: {} (texture usage not taken from it)", relative, world.error().message);
            return;
        }
        if (const auto& terrain = world.value().terrain)
        {
            // Terrain paths are VFS paths, i.e. relative to the source root.
            m_dataImages.insert(terrain->splatMaps.begin(), terrain->splatMaps.end());
            for (const world::TerrainLayerRef& layer : terrain->layers)
            {
                m_colorImages.insert(layer.albedo);
                if (!layer.normal.empty())
                {
                    m_normalImages.insert(layer.normal);
                }
            }
        }
    }

    void noteUsage(const asset::MeshData& mesh, const fs::Path& location, i32 index,
                   std::set<std::string>& into)
    {
        if (index < 0 || static_cast<usize>(index) >= mesh.images.size() ||
            mesh.images[static_cast<usize>(index)].uri.empty())
        {
            return;
        }
        if (auto path = resolveTexture(location, mesh.images[static_cast<usize>(index)].uri))
        {
            into.insert(std::move(path).value());
        }
    }

    /// Encodes an embedded image of `mesh` to KTX2, as normal map if a material uses it so.
    static Result<std::vector<u8>> encodeEmbedded(const asset::MeshData& mesh, usize index, u32 uastcLevel)
    {
        const auto idx = static_cast<i32>(index);
        bool asNormal = false;
        bool asColor = false;
        for (const asset::MaterialInfo& m : mesh.materials)
        {
            asNormal = asNormal || m.normalImage == idx;
            asColor = asColor || m.baseColorImage == idx || m.emissiveImage == idx;
        }
        if (asNormal && asColor)
        {
            return Error{"used both as colour texture and as normal map"};
        }
        auto decoded = asset::decodeImage(mesh.images[index].encoded, "embedded image");
        if (!decoded)
        {
            return decoded.error();
        }
        return encodeKtx2(decoded.value(), asNormal ? TextureUsage::Normal : TextureUsage::Color, uastcLevel);
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
            m_emitted.push_back({path, hashBytes(data), data.size()});
            m_keys.emplace(toLower(path), path);
            m_outputs.emplace(std::move(path), std::move(data));
        }
        return true;
    }

    // --- incremental cooking (manifest) ---

    /// Options that change outputs; a manifest written with other options is not reused.
    std::string optionsString() const
    {
        const bool ktx2 = m_options.textures == TextureMode::Ktx2;
        return std::string("textures=") + (ktx2 ? "ktx2" : "copy") +
               " uastc=" + (ktx2 ? std::to_string(m_options.uastcLevel) : std::string("-")) +
               " output=" + (m_options.pack.empty() ? std::string("loose") : "pack:" + m_options.pack);
    }

    void loadPreviousManifest()
    {
        if (m_options.full)
        {
            return;
        }
        const fs::Path path = m_options.out / fs::fromUtf8(kManifestPath);
        if (!fs::exists(path))
        {
            return;
        }
        auto text = fs::readText(path);
        auto parsed = text ? parseManifest(text.value()) : Result<Manifest>(text.error());
        if (!parsed)
        {
            G7_LOG_WARN("cook", "ignoring manifest ({}), cooking everything", parsed.error().message);
            return;
        }
        if (parsed.value().cookerVersion != kCookerVersion || parsed.value().options != m_new.options)
        {
            G7_LOG_INFO("cook", "cooker version or options changed, cooking everything");
            return;
        }
        m_previous = std::move(parsed).value();
        if (!m_options.pack.empty())
        {
            const fs::Path pak = m_options.out / fs::fromUtf8(m_options.pack);
            m_previousPak = std::make_unique<asset::Vfs>();
            if (!fs::exists(pak) || !m_previousPak->mount(pak, 0))
            {
                m_previousPak.reset(); // nothing to reuse from
            }
        }
    }

    /// glTF buffers and images referenced by URI, with content hashes (0 = missing).
    std::vector<ManifestDependency> meshDependencies(const fs::Path& location,
                                                     std::span<const u8> bytes) const
    {
        std::map<std::string, u64> deps;
        for (const std::string& uri : gltfExternalUris(bytes))
        {
            const fs::Path referenced = (location.parent_path() / fs::fromUtf8(uri)).lexically_normal();
            auto path = asset::normalizeVfsPath(fs::toUtf8(referenced.lexically_relative(m_options.source)));
            if (!path || !isWithin(referenced, m_options.source))
            {
                continue; // reported as error when the mesh is cooked
            }
            auto data = fs::readFile(referenced);
            deps[std::move(path).value()] = data ? hashBytes(data.value()) : 0;
        }
        std::vector<ManifestDependency> out;
        for (auto& [path, hash] : deps)
        {
            out.push_back({path, hash});
        }
        return out;
    }

    /// For KTX2 the output of an image depends on how meshes and worlds use it.
    std::string textureUsageTag(const std::string& relative, const std::string& ext) const
    {
        if (m_options.textures != TextureMode::Ktx2 || !contains(kImageExtensions, ext))
        {
            return {};
        }
        const bool normal = m_normalImages.contains(relative);
        const bool color = m_colorImages.contains(relative);
        const bool data = m_dataImages.contains(relative);
        return std::string(normal ? "normal" : "") + (color ? "color" : "") + (data ? "data" : "");
    }

    u64 sourceKey(const std::string& relative, std::span<const u8> bytes,
                  const std::vector<ManifestDependency>& deps, const std::string& usage) const
    {
        u64 h = hashText("g7cook " + std::to_string(kCookerVersion) + " " + m_new.options);
        h = hashText(relative, h);
        h = hashBytes(bytes, h);
        for (const auto& d : deps)
        {
            h = hashText(d.path, h);
            const u64 dh = d.hash;
            h = hashBytes(std::span(reinterpret_cast<const u8*>(&dh), sizeof(dh)), h);
        }
        return hashText(usage, h);
    }

    /// Takes over the previous outputs of a source whose key did not change (verified by hash).
    bool reusePrevious(const std::string& relative, ManifestEntry& entry)
    {
        if (!m_previous)
        {
            return false;
        }
        const auto it = m_previous->sources.find(relative);
        if (it == m_previous->sources.end() || it->second.key != entry.key)
        {
            return false;
        }
        std::vector<std::pair<std::string, std::vector<u8>>> files;
        for (const ManifestOutput& out : it->second.outputs)
        {
            auto data = previousOutput(out.path);
            if (!data || data->size() != out.size || hashBytes(*data) != out.hash)
            {
                return false; // missing or changed by hand: cook again
            }
            files.emplace_back(out.path, std::move(*data));
        }
        if (emitAll(relative, std::move(files)))
        {
            if (m_options.pack.empty())
            {
                for (const ManifestOutput& out : it->second.outputs)
                {
                    m_unchanged.insert(out.path);
                }
            }
            entry.outputs = std::move(m_emitted);
            m_new.sources.emplace(relative, std::move(entry));
            ++m_report.reused;
        }
        return true; // a collision was reported by emitAll; cooking again would collide too
    }

    /// True if every source was reused and none was removed: the outputs equal the previous ones.
    bool nothingChanged() const
    {
        if (!m_previous || m_report.cooked != 0 || m_previous->sources.size() != m_new.sources.size())
        {
            return false;
        }
        return std::equal(m_previous->sources.begin(), m_previous->sources.end(), m_new.sources.begin(),
                          [](const auto& a, const auto& b)
                          { return a.first == b.first && a.second.key == b.second.key; });
    }

    std::optional<std::vector<u8>> previousOutput(const std::string& path) const
    {
        if (!m_options.pack.empty())
        {
            if (!m_previousPak)
            {
                return std::nullopt;
            }
            auto data = m_previousPak->read(path);
            return data ? std::optional(std::move(data).value()) : std::nullopt;
        }
        const fs::Path target = (m_options.out / fs::fromUtf8(path)).lexically_normal();
        if (!isWithin(target, m_options.out))
        {
            return std::nullopt; // never reuse files from outside <out>
        }
        auto data = fs::readFile(target);
        return data ? std::optional(std::move(data).value()) : std::nullopt;
    }

    static bool sameContent(const fs::Path& target, const std::vector<u8>& data)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(target, ec) ||
            std::filesystem::file_size(target, ec) != data.size())
        {
            return false;
        }
        auto existing = fs::readFile(target);
        return existing && existing.value() == data;
    }

    /// Loose mode: deletes outputs listed in the previous manifest that are no longer produced.
    void removeStaleOutputs()
    {
        if (!m_previous)
        {
            return;
        }
        for (const auto& [source, entry] : m_previous->sources)
        {
            for (const ManifestOutput& out : entry.outputs)
            {
                if (m_keys.contains(toLower(out.path)))
                {
                    continue; // still produced (possibly in another spelling)
                }
                // Second line of defence besides parseManifest: never delete outside <out>.
                const fs::Path target = (m_options.out / fs::fromUtf8(out.path)).lexically_normal();
                if (!isWithin(target, m_options.out))
                {
                    G7_LOG_WARN("cook", "manifest output '{}' lies outside the output directory, not removed",
                                out.path);
                    continue;
                }
                std::error_code ec;
                if (std::filesystem::remove(target, ec))
                {
                    G7_LOG_INFO("cook", "removed stale output {}", out.path);
                }
            }
        }
    }

    Result<void> writeManifest() const
    {
        const fs::Path path = m_options.out / fs::fromUtf8(kManifestPath);
        if (auto created = fs::createDirectories(path.parent_path()); !created)
        {
            return created.error();
        }
        return fs::writeTextAtomic(path, serializeManifest(m_new));
    }

    void fail(const std::string& path, const std::string& message)
    {
        G7_LOG_ERROR("cook", "{}: {}", path, message);
        m_report.errors.push_back(path + ": " + message);
    }

    const CookOptions& m_options;
    std::map<std::string, std::vector<u8>> m_outputs;        // sorted by path
    std::optional<Manifest> m_previous;                      // valid previous manifest, if any
    std::unique_ptr<asset::Vfs> m_previousPak;               // previous archive (pack mode)
    Manifest m_new;                                          // written after a successful cook
    std::vector<ManifestOutput> m_emitted;                   // outputs of the source being cooked
    std::set<std::string> m_unchanged;                       // loose outputs reused as they are
    std::map<std::string, Result<asset::MeshData>> m_meshes; // parsed in scanTextureUsage (KTX2 mode)
    std::set<std::string> m_normalImages;                    // VFS paths used as normal maps
    std::set<std::string> m_colorImages;                     // VFS paths used as colour textures
    std::set<std::string> m_dataImages;                      // VFS paths used as terrain splat maps
    std::map<std::string, std::string> m_keys;               // lower-case path -> path (collisions)
    CookReport m_report;
    struct Ktx2Job
    {
        std::string name;
        std::vector<u8> bytes;
        TextureUsage usage = TextureUsage::Color;
        std::promise<Result<std::vector<u8>>> result;
    };
    std::vector<std::unique_ptr<Ktx2Job>> m_ktx2Jobs;                        // fixed once workers start
    std::map<std::string, std::future<Result<std::vector<u8>>>> m_ktx2Ahead; // by source path
    std::atomic<usize> m_ktx2Next{0};
    std::vector<std::jthread> m_ktx2Workers; // last: joined before the jobs go
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

    if (options.textures == TextureMode::Ktx2 && !hasKtx2Encoder())
    {
        return Error{"--textures ktx2 needs a build with libktx (vcpkg); this one has none"};
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
    G7_LOG_INFO(
        "cook",
        "{} cooked ({} meshes, {} images, {} copied), {} reused, {} skipped -> {} outputs ({} errors)",
        report.cooked, report.meshes, report.images, report.copied, report.reused, report.skipped,
        report.outputs, report.errors.size());
    return report;
}
} // namespace g7::cook

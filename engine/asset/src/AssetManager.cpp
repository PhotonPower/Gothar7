#include <g7/asset/AssetManager.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/asset/MeshFile.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace g7::asset
{
Result<std::vector<u8>> LoadContext::read(std::string_view otherPath) const
{
    if (dependencies != nullptr)
    {
        dependencies->emplace_back(otherPath);
    }
    return vfs->read(otherPath);
}

std::string LoadContext::sibling(std::string_view relative) const
{
    const auto slash = path.rfind('/');
    if (slash == std::string_view::npos)
    {
        return std::string(relative);
    }
    return std::string(path.substr(0, slash + 1)) + std::string(relative);
}

std::optional<fs::Path> LoadContext::diskPath() const
{
    return vfs->diskPath(path);
}

namespace
{
using detail::AssetSlot;
using SlotPtr = std::shared_ptr<AssetSlot>;

struct CacheKey
{
    std::string path; // lower-case
    std::type_index type;
    bool operator==(const CacheKey&) const = default;
};

struct CacheKeyHash
{
    usize operator()(const CacheKey& k) const noexcept
    {
        return std::hash<std::string>{}(k.path) ^ (k.type.hash_code() * 0x9e3779b97f4a7c15ull);
    }
};

void fail(AssetSlot& slot, std::string message)
{
    slot.error = std::move(message);
    slot.state.store(AssetState::Failed, std::memory_order_release);
}

bool hasExtension(std::string_view path, std::string_view extension) noexcept
{
    return path.size() >= extension.size() &&
           equalsIgnoreCase(path.substr(path.size() - extension.size()), extension);
}

/// A loose file an asset was built from and its modification time when it was read.
struct FileStamp
{
    fs::Path disk;
    std::filesystem::file_time_type time;
};

std::optional<FileStamp> stampOf(const Vfs& vfs, std::string_view path)
{
    auto disk = vfs.diskPath(path);
    if (!disk)
    {
        return std::nullopt; // inside an archive: not watched
    }
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(*disk, ec);
    if (ec)
    {
        return std::nullopt;
    }
    return FileStamp{std::move(*disk), time};
}

SlotPtr failedSlot(std::string path, std::type_index type, std::string message)
{
    G7_LOG_WARN("asset", "{}", message);
    auto slot = std::make_shared<AssetSlot>(std::move(path), type);
    fail(*slot, std::move(message));
    return slot;
}
} // namespace

struct AssetManager::Impl
{
    struct Job
    {
        SlotPtr slot;
        ErasedLoader loader;
        bool reload = false;
    };
    struct Done
    {
        SlotPtr slot;
        ErasedResult result;
        bool reload = false;
        std::vector<FileStamp> files; // the asset's loose file and its dependencies, as read
    };

    const Vfs& vfs;
    std::unordered_map<std::type_index, ErasedLoader> loaders;
    std::unordered_map<CacheKey, std::weak_ptr<AssetSlot>, CacheKeyHash> cache;

    mutable std::mutex mutex; // guards queue, done, inFlight, stopping
    std::condition_variable workAvailable;
    std::condition_variable idle;
    std::deque<Job> queue;
    std::vector<Done> done;
    usize inFlight = 0;
    bool stopping = false;
    std::vector<std::jthread> workers;

    // Hot reload (main thread only).
    std::unordered_map<std::string, std::vector<FileStamp>> watches; // lower-case path -> files
    bool hotReload = false;
    f64 pollSeconds = 0.5;
    f64 lastPoll = -1e30;

    explicit Impl(const Vfs& v) : vfs(v) {}

    /// Reads the file and runs the loader (worker thread or synchronous mode). Records the loose
    /// files involved with their times *before* reading, so a later change is never missed.
    [[nodiscard]] Done execute(const Job& job) const
    {
        Done outcome{job.slot, Error{""}, job.reload, {}};
        if (auto stamp = stampOf(vfs, job.slot->path))
        {
            outcome.files.push_back(std::move(*stamp));
        }
        auto bytes = vfs.read(job.slot->path);
        if (!bytes)
        {
            outcome.result = Error{"cannot load '" + job.slot->path + "': " + bytes.error().message};
            return outcome;
        }
        std::vector<std::string> dependencies;
        const LoadContext ctx{job.slot->path, bytes.value(), &vfs, &dependencies};
        outcome.result = job.loader(ctx);
        if (!outcome.result)
        {
            outcome.result = Error{"cannot load '" + job.slot->path + "': " + outcome.result.error().message};
        }
        for (const std::string& dependency : dependencies)
        {
            if (auto stamp = stampOf(vfs, dependency))
            {
                outcome.files.push_back(std::move(*stamp));
            }
        }
        return outcome;
    }

    void workerLoop()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock lock(mutex);
                workAvailable.wait(lock, [this] { return stopping || !queue.empty(); });
                if (stopping)
                {
                    return;
                }
                job = std::move(queue.front());
                queue.pop_front();
                ++inFlight;
            }
            Done result = execute(job);
            {
                std::lock_guard lock(mutex);
                done.push_back(std::move(result));
                --inFlight;
            }
            idle.notify_all();
        }
    }

    /// Main thread: makes finished loads and reloads visible.
    void publish(std::vector<Done>& finished)
    {
        for (Done& d : finished)
        {
            if (d.result)
            {
                d.slot->data = std::move(d.result).value();
                ++d.slot->version;
                d.slot->state.store(AssetState::Ready, std::memory_order_release);
                if (d.reload)
                {
                    G7_LOG_INFO("asset", "reloaded '{}' (version {})", d.slot->path, d.slot->version);
                }
            }
            else if (d.reload && d.slot->state.load(std::memory_order_acquire) == AssetState::Ready)
            {
                // E.g. a file caught while being saved: keep what works, retry on the next change.
                G7_LOG_WARN("asset", "reload failed, keeping version {}: {}", d.slot->version,
                            d.result.error().message);
            }
            else
            {
                G7_LOG_WARN("asset", "{}", d.result.error().message);
                fail(*d.slot, d.result.error().message);
            }
            if (!d.files.empty())
            {
                watches[toLower(d.slot->path)] = std::move(d.files);
            }
        }
    }

    void pruneCache()
    {
        std::erase_if(cache, [](const auto& entry) { return entry.second.expired(); });
        // Released assets are no longer watched.
        std::erase_if(watches,
                      [this](const auto& watch)
                      {
                          return std::none_of(cache.begin(), cache.end(), [&](const auto& entry)
                                              { return entry.first.path == watch.first; });
                      });
    }
};

AssetManager::AssetManager(const Vfs& vfs, AssetManagerDesc desc) : m_impl(std::make_unique<Impl>(vfs))
{
    m_impl->hotReload = desc.hotReload;
    m_impl->pollSeconds = desc.pollSeconds;
    registerLoader<ImageData>([](const LoadContext& ctx) -> Result<ImageData>
                              { return decodeImage(ctx.bytes, ctx.path); });
    // Textures as the renderer uploads them: cooked KTX2 (BC7/BC5 with mips) or PNG/JPEG as RGBA8.
    registerLoader<TextureData>(
        [](const LoadContext& ctx) -> Result<TextureData>
        {
            if (hasExtension(ctx.path, ".ktx2"))
            {
                return decodeKtx2(ctx.bytes, ctx.path);
            }
            auto image = decodeImage(ctx.bytes, ctx.path);
            if (!image)
            {
                return image.error();
            }
            return textureFromImage(std::move(image).value());
        });
    registerLoader<MeshData>(
        [](const LoadContext& ctx) -> Result<MeshData>
        {
            // Cooked meshes (ADR 0016); glTF stays loadable for development (loose files).
            if (hasExtension(ctx.path, ".g7mesh"))
            {
                return deserializeMesh(ctx.bytes, ctx.path);
            }
            // External buffers are read from a directory on disk, so they work for loose files only;
            // archived glTF must be self-contained (.glb, data: URIs).
            const auto disk = ctx.diskPath();
            return loadGltf(ctx.bytes, disk ? disk->parent_path() : fs::Path(), ctx.path);
        });
    for (u32 i = 0; i < desc.workerThreads; ++i)
    {
        m_impl->workers.emplace_back([impl = m_impl.get()] { impl->workerLoop(); });
    }
}

AssetManager::~AssetManager()
{
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->workAvailable.notify_all();
    m_impl->workers.clear(); // joins; a running loader finishes first
    for (Impl::Job& job : m_impl->queue)
    {
        fail(*job.slot, "asset manager shut down before '" + job.slot->path + "' was loaded");
    }
    for (Impl::Done& d : m_impl->done)
    {
        fail(*d.slot, "asset manager shut down before '" + d.slot->path + "' was published");
    }
}

void AssetManager::registerErased(std::type_index type, ErasedLoader loader)
{
    m_impl->loaders[type] = std::move(loader);
}

std::shared_ptr<detail::AssetSlot> AssetManager::loadErased(std::string_view path, std::type_index type)
{
    auto normalised = normalizeVfsPath(path);
    if (!normalised)
    {
        return failedSlot(std::string(path), type, "cannot load asset: " + normalised.error().message);
    }
    CacheKey key{toLower(normalised.value()), type};
    if (auto it = m_impl->cache.find(key); it != m_impl->cache.end())
    {
        if (SlotPtr existing = it->second.lock())
        {
            return existing;
        }
    }
    const auto loader = m_impl->loaders.find(type);
    if (loader == m_impl->loaders.end())
    {
        return failedSlot(std::move(normalised).value(), type,
                          "cannot load '" + std::string(path) + "': no loader registered for " + type.name());
    }

    auto slot = std::make_shared<detail::AssetSlot>(std::move(normalised).value(), type);
    m_impl->cache[std::move(key)] = slot;
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->queue.push_back({slot, loader->second});
    }
    m_impl->workAvailable.notify_one();
    return slot;
}

void AssetManager::update()
{
    std::vector<Impl::Done> finished;
    if (m_impl->workers.empty())
    {
        // Synchronous mode: run everything queued so far on this thread.
        std::deque<Impl::Job> jobs;
        {
            std::lock_guard lock(m_impl->mutex);
            jobs.swap(m_impl->queue);
        }
        for (Impl::Job& job : jobs)
        {
            finished.push_back(m_impl->execute(job));
        }
    }
    {
        std::lock_guard lock(m_impl->mutex);
        for (Impl::Done& d : m_impl->done)
        {
            finished.push_back(std::move(d));
        }
        m_impl->done.clear();
    }
    m_impl->publish(finished);
    m_impl->pruneCache();
}

void AssetManager::waitAll()
{
    if (!m_impl->workers.empty())
    {
        std::unique_lock lock(m_impl->mutex);
        m_impl->idle.wait(lock, [this] { return m_impl->queue.empty() && m_impl->inFlight == 0; });
    }
    update();
}

usize AssetManager::reload(std::string_view path)
{
    auto normalised = normalizeVfsPath(path);
    if (!normalised)
    {
        return 0;
    }
    const std::string key = toLower(normalised.value());
    usize started = 0;
    for (const auto& [cacheKey, weak] : m_impl->cache)
    {
        const SlotPtr slot = weak.lock();
        // A first load still in flight reads the current file anyway.
        if (!slot || cacheKey.path != key ||
            slot->state.load(std::memory_order_acquire) == AssetState::Loading)
        {
            continue;
        }
        const auto loader = m_impl->loaders.find(cacheKey.type);
        if (loader == m_impl->loaders.end())
        {
            continue;
        }
        {
            std::lock_guard lock(m_impl->mutex);
            m_impl->queue.push_back({slot, loader->second, true});
        }
        m_impl->workAvailable.notify_one();
        ++started;
    }
    return started;
}

u32 AssetManager::checkForChanges(f64 now)
{
    if (!m_impl->hotReload || now - m_impl->lastPoll < m_impl->pollSeconds)
    {
        return 0;
    }
    m_impl->lastPoll = now;
    std::vector<std::string> changed;
    for (auto& [path, files] : m_impl->watches)
    {
        bool modified = false;
        for (FileStamp& file : files)
        {
            std::error_code ec;
            const auto time = std::filesystem::last_write_time(file.disk, ec);
            if (!ec && time != file.time)
            {
                file.time = time; // trigger once per change, even while the reload is running
                modified = true;
            }
        }
        if (modified)
        {
            changed.push_back(path);
        }
    }
    u32 started = 0;
    for (const std::string& path : changed)
    {
        started += reload(path) > 0 ? 1u : 0u;
    }
    return started;
}

void AssetManager::setHotReload(bool enabled) noexcept
{
    m_impl->hotReload = enabled;
}

bool AssetManager::hotReload() const noexcept
{
    return m_impl->hotReload;
}

usize AssetManager::pendingCount() const
{
    std::lock_guard lock(m_impl->mutex);
    return m_impl->queue.size() + m_impl->inFlight + m_impl->done.size();
}

usize AssetManager::cachedCount() const
{
    usize count = 0;
    for (const auto& [key, slot] : m_impl->cache)
    {
        count += slot.expired() ? 0 : 1;
    }
    return count;
}
} // namespace g7::asset

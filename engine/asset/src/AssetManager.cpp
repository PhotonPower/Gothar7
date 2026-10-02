#include <g7/asset/AssetManager.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace g7::asset
{
Result<std::vector<u8>> LoadContext::read(std::string_view otherPath) const
{
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
    };
    struct Done
    {
        SlotPtr slot;
        ErasedResult result;
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

    explicit Impl(const Vfs& v) : vfs(v) {}

    /// Reads the file and runs the loader (worker thread or synchronous mode).
    [[nodiscard]] ErasedResult execute(const Job& job) const
    {
        auto bytes = vfs.read(job.slot->path);
        if (!bytes)
        {
            return Error{"cannot load '" + job.slot->path + "': " + bytes.error().message};
        }
        const LoadContext ctx{job.slot->path, bytes.value(), &vfs};
        auto result = job.loader(ctx);
        if (!result)
        {
            return Error{"cannot load '" + job.slot->path + "': " + result.error().message};
        }
        return result;
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
            ErasedResult result = execute(job);
            {
                std::lock_guard lock(mutex);
                done.push_back({std::move(job.slot), std::move(result)});
                --inFlight;
            }
            idle.notify_all();
        }
    }

    /// Main thread: makes finished loads visible.
    void publish(std::vector<Done>& finished)
    {
        for (Done& d : finished)
        {
            if (d.result)
            {
                d.slot->data = std::move(d.result).value();
                ++d.slot->version;
                d.slot->state.store(AssetState::Ready, std::memory_order_release);
            }
            else
            {
                G7_LOG_WARN("asset", "{}", d.result.error().message);
                fail(*d.slot, d.result.error().message);
            }
        }
    }

    void pruneCache()
    {
        std::erase_if(cache, [](const auto& entry) { return entry.second.expired(); });
    }
};

AssetManager::AssetManager(const Vfs& vfs, AssetManagerDesc desc) : m_impl(std::make_unique<Impl>(vfs))
{
    registerLoader<ImageData>([](const LoadContext& ctx) -> Result<ImageData>
                              { return decodeImage(ctx.bytes, ctx.path); });
    registerLoader<MeshData>(
        [](const LoadContext& ctx) -> Result<MeshData>
        {
            // External buffers are read from a directory on disk, so they work for loose files only;
            // archived meshes must be self-contained (.glb, data: URIs), as the cooker writes them.
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
            ErasedResult result = m_impl->execute(job);
            finished.push_back({std::move(job.slot), std::move(result)});
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

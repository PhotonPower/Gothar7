#pragma once

#include <g7/asset/Vfs.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

namespace g7::asset
{
enum class AssetState : u8
{
    Loading,
    Ready,
    Failed
};

namespace detail
{
/// Shared state of one cached asset; handles keep it alive (reference counting).
struct AssetSlot
{
    AssetSlot(std::string slotPath, std::type_index slotType) : path(std::move(slotPath)), type(slotType) {}

    std::string path; // normalised VFS path
    std::type_index type;
    std::atomic<AssetState> state{AssetState::Loading};
    std::shared_ptr<void> data; // the T, set before state becomes Ready
    std::string error;          // set before state becomes Failed
    u32 version = 0;
};
} // namespace detail

/// Reference-counted handle to an asset of type T. Copies share the asset; the asset is released
/// when the last handle goes away. State changes happen only in AssetManager::update(), so on the
/// main thread an asset never changes in the middle of a frame.
template <typename T>
class Handle
{
public:
    Handle() = default;

    [[nodiscard]] bool valid() const noexcept { return m_slot != nullptr; }
    [[nodiscard]] AssetState state() const noexcept
    {
        return m_slot ? m_slot->state.load(std::memory_order_acquire) : AssetState::Failed;
    }
    [[nodiscard]] bool isReady() const noexcept { return state() == AssetState::Ready; }
    [[nodiscard]] bool failed() const noexcept { return state() == AssetState::Failed; }

    /// The asset, or nullptr while loading or after a failure (show a placeholder meanwhile).
    [[nodiscard]] const T* get() const noexcept
    {
        return isReady() ? static_cast<const T*>(m_slot->data.get()) : nullptr;
    }
    [[nodiscard]] const T* operator->() const noexcept { return get(); }

    [[nodiscard]] const std::string& path() const noexcept { return m_slot ? m_slot->path : emptyString(); }
    /// Error message once failed, else empty.
    [[nodiscard]] const std::string& error() const noexcept
    {
        return failed() && m_slot ? m_slot->error : emptyString();
    }
    /// 1 after the first successful load; hot reload (M3) increments it.
    [[nodiscard]] u32 version() const noexcept { return isReady() ? m_slot->version : 0; }

    /// Number of handles sharing the asset (tests, diagnostics).
    [[nodiscard]] long useCount() const noexcept { return m_slot ? m_slot.use_count() : 0; }

    friend bool operator==(const Handle& a, const Handle& b) noexcept { return a.m_slot == b.m_slot; }

private:
    friend class AssetManager;
    explicit Handle(std::shared_ptr<detail::AssetSlot> slot) : m_slot(std::move(slot)) {}

    static const std::string& emptyString() noexcept
    {
        static const std::string kEmpty;
        return kEmpty;
    }

    std::shared_ptr<detail::AssetSlot> m_slot;
};

/// What a loader gets on the worker thread.
struct LoadContext
{
    std::string_view path;     ///< Normalised VFS path of the asset.
    std::span<const u8> bytes; ///< File contents.
    const Vfs* vfs = nullptr;

    /// Reads another file through the VFS (dependencies such as buffers or included files).
    [[nodiscard]] Result<std::vector<u8>> read(std::string_view otherPath) const;
    /// Path of a file next to this asset ("textures/a.png" for "textures/b.mat" + "a.png").
    [[nodiscard]] std::string sibling(std::string_view relative) const;
    /// Location on disk if the asset is a loose file (not inside a .g7pak archive).
    [[nodiscard]] std::optional<fs::Path> diskPath() const;
};

template <typename T>
using Loader = std::function<Result<T>(const LoadContext&)>;

struct AssetManagerDesc
{
    /// Worker threads that read and decode assets. 0 = no threads: loads run inside update()
    /// on the calling thread (tools, deterministic tests).
    u32 workerThreads = 2;
};

/// Loads assets of registered types through a Vfs, asynchronously and with a cache
/// (docs/modules/asset.md). load() returns at once; workers read the file and run the type's
/// loader; update() (main thread) publishes finished loads. The same path (case-insensitive)
/// and type yields the same shared asset while any handle to it lives.
///
/// Registered by default: ImageData (PNG/JPEG/TGA/BMP) and MeshData (glTF/.glb).
/// The Vfs must outlive the manager. load(), update() and registerLoader() belong to the main
/// thread; loaders run on worker threads and must only use their LoadContext.
class AssetManager
{
public:
    explicit AssetManager(const Vfs& vfs, AssetManagerDesc desc = {});
    /// Stops the workers; loads that were not published yet end as Failed.
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    template <typename T>
    void registerLoader(Loader<T> loader)
    {
        registerErased(typeid(T),
                       [loader = std::move(loader)](const LoadContext& ctx) -> ErasedResult
                       {
                           auto result = loader(ctx);
                           if (!result)
                           {
                               return result.error();
                           }
                           return std::shared_ptr<void>(std::make_shared<T>(std::move(result).value()));
                       });
    }

    /// Starts loading (or returns the cached asset). Invalid paths and types without a loader
    /// give a handle that is Failed right away.
    template <typename T>
    [[nodiscard]] Handle<T> load(std::string_view path)
    {
        return Handle<T>(loadErased(path, typeid(T)));
    }

    /// Publishes finished loads (Loading -> Ready/Failed). Call once per frame on the main thread.
    void update();
    /// Blocks until no load is pending, then publishes everything (loading screens, tests).
    void waitAll();

    /// Loads started but not yet published.
    [[nodiscard]] usize pendingCount() const;
    /// Assets currently held by at least one handle.
    [[nodiscard]] usize cachedCount() const;

private:
    using ErasedResult = Result<std::shared_ptr<void>>;
    using ErasedLoader = std::function<ErasedResult(const LoadContext&)>;

    void registerErased(std::type_index type, ErasedLoader loader);
    [[nodiscard]] std::shared_ptr<detail::AssetSlot> loadErased(std::string_view path, std::type_index type);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace g7::asset

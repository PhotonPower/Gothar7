#pragma once

// GPU textures shared by every MaterialSet that uses the same image file: many models referencing a few
// trim sheets upload each sheet once (render.md "Textur-Cache"). Entries are reference counted by the
// MaterialSets holding them; the cache keeps only weak references, so a texture goes when its last user goes.

#include <g7/asset/TextureData.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace g7::render
{
class Device;

class TextureCache
{
public:
    /// The texture of `data` (sRGB or linear) under `key` (the image's VFS path) and `version` (asset
    /// version: a hot-reloaded image gets a new texture, the old one stays with the models not yet updated).
    /// Uploads on the first request.
    [[nodiscard]] Result<std::shared_ptr<const rhi::Texture>> get(Device& device, std::string_view key,
                                                                  u64 version, const asset::TextureData& data,
                                                                  bool srgb,
                                                                  std::optional<f32> alphaCutoff = {});
    /// Textures still in use (expired entries are dropped).
    [[nodiscard]] usize size();
    /// Uploads done so far (tests, statistics).
    [[nodiscard]] u64 uploads() const noexcept { return m_uploads; }

private:
    std::map<std::string, std::weak_ptr<const rhi::Texture>, std::less<>> m_entries;
    u64 m_uploads = 0;
    u32 m_requestsSincePrune = 0;
};
} // namespace g7::render

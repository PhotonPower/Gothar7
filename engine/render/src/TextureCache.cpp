#include <g7/render/Device.hpp>
#include <g7/render/TextureCache.hpp>
#include <g7/render/TextureUpload.hpp>

#include <format>

namespace g7::render
{
Result<std::shared_ptr<const rhi::Texture>> TextureCache::get(Device& device, std::string_view key,
                                                              u64 version, const asset::TextureData& data,
                                                              bool srgb)
{
    const std::string id = std::format("{}|{}|{}", key, srgb ? "srgb" : "linear", version);
    if (const auto it = m_entries.find(id); it != m_entries.end())
    {
        if (auto texture = it->second.lock())
        {
            return std::shared_ptr<const rhi::Texture>(std::move(texture));
        }
    }
    auto texture = createTexture(device, data, srgb);
    if (!texture)
    {
        return texture.error();
    }
    ++m_uploads;
    auto shared = std::make_shared<const rhi::Texture>(std::move(texture).value());
    m_entries[id] = shared;
    if (++m_requestsSincePrune >= 256) // keep the map from growing with dead entries (reloads, world changes)
    {
        (void)size();
    }
    return std::shared_ptr<const rhi::Texture>(std::move(shared));
}

usize TextureCache::size()
{
    m_requestsSincePrune = 0;
    std::erase_if(m_entries, [](const auto& entry) { return entry.second.expired(); });
    return m_entries.size();
}
} // namespace g7::render

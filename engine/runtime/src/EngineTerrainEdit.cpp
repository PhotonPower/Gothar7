// Terrain editing for the editor (M16 part F2, ADR 0023): heights and splat weights changed in place, the
// drawn terrain following at once, the collision after a stroke, saving over the sources. Worlds with a
// generator head (Leonberg's LGL terrain from welt) stay locked.

#include "PlayerFigure.hpp"

#include <g7/asset/ImageData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Log.hpp>
#include <g7/runtime/AssetMounts.hpp>
#include <g7/runtime/Engine.hpp>

#include <filesystem>
#include <format>

namespace g7
{
std::string Engine::terrainLock() const
{
    if (!m_hasTerrain)
    {
        return "the world has no terrain";
    }
    if (!m_worldFile.generatorJson.empty())
    {
        return "the terrain comes from the world's generator (Leonberg: LGL data via welt) - not edited here";
    }
    return {};
}

world::Heightfield* Engine::editableHeightfield() noexcept
{
    return terrainLock().empty() ? &m_heightfield : nullptr;
}

Result<void> Engine::terrainHeightsChanged()
{
    if (!m_hasTerrain)
    {
        return Error{"no terrain"};
    }
    if (m_device)
    {
        return m_terrain.updateHeights(m_heightfield.renderDesc());
    }
    return {};
}

void Engine::terrainStrokeFinished()
{
    m_physicsDirty = true; // the heightfield body anew - now, the editor pauses the simulation
    syncPhysics();
}

std::optional<Engine::SplatView> Engine::terrainSplat(u32 map) noexcept
{
    if (!m_hasTerrain || map >= m_heightfield.ref().splatMaps.size())
    {
        return std::nullopt;
    }
    if (m_splatImages.size() != m_heightfield.ref().splatMaps.size())
    {
        // Not loaded for the footsteps: the weights on the CPU now (PNG, or the cooked KTX2 decoded).
        m_splatImages.clear();
        for (const std::string& source : m_heightfield.ref().splatMaps)
        {
            const std::string path = preferCooked(m_vfs, source);
            auto bytes = m_vfs.read(path);
            auto image = !bytes                    ? Result<asset::ImageData>(bytes.error())
                         : path.ends_with(".ktx2") ? asset::decodeKtx2Rgba(bytes.value(), path)
                                                   : asset::decodeImage(bytes.value(), path);
            if (!image)
            {
                G7_LOG_WARN("engine", "splat map {}: {}", path, image.error().message);
                m_splatImages.clear();
                return std::nullopt;
            }
            m_splatImages.push_back(std::move(image).value());
        }
    }
    asset::ImageData& image = m_splatImages[map];
    return SplatView{image.rgba8, image.width, image.height};
}

Result<void> Engine::terrainSplatChanged()
{
    if (!m_hasTerrain || m_splatImages.empty())
    {
        return Error{"no splat maps"};
    }
    if (m_device)
    {
        loadTerrainSurface(m_heightfield.ref(), true);
    }
    return {};
}

u32 Engine::terrainLayerCount() const noexcept
{
    return m_hasTerrain ? static_cast<u32>(m_heightfield.ref().layers.size()) : 0u;
}

Result<void> Engine::saveTerrain()
{
    if (const std::string locked = terrainLock(); !locked.empty())
    {
        return Error{locked};
    }
    const world::TerrainRef& ref = m_heightfield.ref();
    const auto heights = m_vfs.diskPath(ref.heightmap);
    if (!heights)
    {
        return Error{std::format("{} is not a file on disk (archive) - cannot be written", ref.heightmap)};
    }
    if (auto saved = m_heightfield.saveR16(*heights); !saved)
    {
        return saved;
    }
    for (usize i = 0; i < m_splatImages.size() && i < ref.splatMaps.size(); ++i)
    {
        const auto map = m_vfs.diskPath(ref.splatMaps[i]);
        if (!map)
        {
            return Error{std::format("{} is not a file on disk - cannot be written", ref.splatMaps[i])};
        }
        if (auto saved = asset::savePng(*map, m_splatImages[i]); !saved)
        {
            return saved;
        }
        if (preferCooked(m_vfs, ref.splatMaps[i]) != ref.splatMaps[i])
        {
            G7_LOG_WARN("engine", "{}: a cooked copy is there and wins until it is cooked anew",
                        ref.splatMaps[i]);
        }
    }
    G7_LOG_INFO("engine", "terrain saved: {}", fs::toUtf8(*heights));
    return {};
}
} // namespace g7

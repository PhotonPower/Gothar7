#include <g7/world/TerrainRef.hpp>

#include <algorithm>
#include <cmath>

namespace g7::world
{
f32 decodeHeight(u16 value, f32 minY, f32 maxY) noexcept
{
    return minY + static_cast<f32>(value) / 65535.0f * (maxY - minY);
}

u16 encodeHeight(f32 height, f32 minY, f32 maxY) noexcept
{
    const f32 v = std::round((height - minY) / (maxY - minY) * 65535.0f);
    return static_cast<u16>(std::clamp(v, 0.0f, 65535.0f));
}

std::string grassOverlayPath(std::string_view worldPath)
{
    constexpr std::string_view kExtension = ".g7world";
    if (worldPath.ends_with(kExtension))
    {
        worldPath.remove_suffix(kExtension.size());
    }
    return std::string(worldPath) + ".grass.png";
}
} // namespace g7::world

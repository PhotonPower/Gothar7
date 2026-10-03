#include <g7/asset/Vfs.hpp>
#include <g7/core/Log.hpp>
#include <g7/world/Terrain.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7::world
{
Result<Heightfield> Heightfield::create(const TerrainRef& ref, std::vector<u16> samples)
{
    if (ref.width < 2 || ref.height < 2)
    {
        return Error{std::format("terrain: {} x {} samples, at least 2 x 2 needed", ref.width, ref.height)};
    }
    if (!(ref.cellSize > 0.0f))
    {
        return Error{"terrain: cellSize must be positive"};
    }
    if (!(ref.maxY > ref.minY))
    {
        return Error{"terrain: maxY must be above minY"};
    }
    const usize expected = static_cast<usize>(ref.width) * ref.height;
    if (samples.size() != expected)
    {
        return Error{std::format("terrain: '{}' has {} samples, {} x {} = {} expected", ref.heightmap,
                                 samples.size(), ref.width, ref.height, expected)};
    }
    Heightfield field;
    field.m_ref = ref;
    field.m_samples = std::move(samples);
    return field;
}

Result<Heightfield> Heightfield::load(const asset::Vfs& vfs, const TerrainRef& ref)
{
    auto bytes = vfs.read(ref.heightmap);
    if (!bytes)
    {
        return Error{"terrain: " + bytes.error().message};
    }
    if (bytes.value().size() % 2 != 0)
    {
        return Error{std::format("terrain: '{}' has an odd byte count", ref.heightmap)};
    }
    std::vector<u16> samples(bytes.value().size() / 2);
    for (usize i = 0; i < samples.size(); ++i)
    {
        // Little endian, whatever the machine.
        samples[i] = static_cast<u16>(bytes.value()[2 * i] | (bytes.value()[2 * i + 1] << 8));
    }
    auto field = create(ref, std::move(samples));
    if (!field || ref.holes.empty())
    {
        return field;
    }
    auto mask = vfs.read(ref.holes);
    if (!mask)
    {
        return Error{"terrain holes: " + mask.error().message};
    }
    if (auto holes = field.value().setHoles(std::move(mask).value()); !holes)
    {
        return Error{std::format("terrain holes '{}': {}", ref.holes, holes.error().message)};
    }
    if (std::ranges::all_of(field.value().m_holes, [](u8 v) { return v == 0; }))
    {
        G7_LOG_WARN("world", "terrain holes '{}' contain only zeros: the whole terrain is a hole", ref.holes);
    }
    return field;
}

Result<void> Heightfield::setHoles(std::vector<u8> holes)
{
    const usize cells = static_cast<usize>(m_ref.width - 1) * (m_ref.height - 1);
    if (!holes.empty() && holes.size() != cells)
    {
        return Error{std::format("{} bytes, {} x {} = {} expected (one per cell)", holes.size(),
                                 m_ref.width - 1, m_ref.height - 1, cells)};
    }
    m_holes = std::move(holes);
    return {};
}

bool Heightfield::isHole(f32 x, f32 z) const noexcept
{
    if (m_holes.empty())
    {
        return false;
    }
    const f32 u = (x - m_ref.firstSample.x) / m_ref.cellSize;
    const f32 v = (z - m_ref.firstSample.y) / m_ref.cellSize;
    if (u < 0.0f || v < 0.0f || u > static_cast<f32>(m_ref.width - 1) ||
        v > static_cast<f32>(m_ref.height - 1))
    {
        return false;
    }
    // The last sample line belongs to the last cell.
    const auto c = std::min(static_cast<u32>(u), m_ref.width - 2);
    const auto r = std::min(static_cast<u32>(v), m_ref.height - 2);
    return m_holes[static_cast<usize>(r) * (m_ref.width - 1) + c] == 0;
}

f32 Heightfield::sampleHeight(i32 column, i32 row) const noexcept
{
    const auto c = static_cast<usize>(std::clamp(column, 0, static_cast<i32>(m_ref.width) - 1));
    const auto r = static_cast<usize>(std::clamp(row, 0, static_cast<i32>(m_ref.height) - 1));
    return decodeHeight(m_samples[r * m_ref.width + c], m_ref.minY, m_ref.maxY);
}

f32 Heightfield::heightAt(f32 x, f32 z) const noexcept
{
    if (m_samples.empty())
    {
        return 0.0f;
    }
    // Position in samples, clamped to the area between the outer sample centres.
    const f32 u =
        std::clamp((x - m_ref.firstSample.x) / m_ref.cellSize, 0.0f, static_cast<f32>(m_ref.width - 1));
    const f32 v =
        std::clamp((z - m_ref.firstSample.y) / m_ref.cellSize, 0.0f, static_cast<f32>(m_ref.height - 1));
    const auto c = static_cast<i32>(std::floor(u));
    const auto r = static_cast<i32>(std::floor(v));
    const f32 fu = u - static_cast<f32>(c);
    const f32 fv = v - static_cast<f32>(r);
    const f32 top = glm::mix(sampleHeight(c, r), sampleHeight(c + 1, r), fu);
    const f32 bottom = glm::mix(sampleHeight(c, r + 1), sampleHeight(c + 1, r + 1), fu);
    return glm::mix(top, bottom, fv);
}

Vec3 Heightfield::normalAt(f32 x, f32 z) const noexcept
{
    const f32 d = m_ref.cellSize;
    const f32 dx = heightAt(x + d, z) - heightAt(x - d, z);
    const f32 dz = heightAt(x, z + d) - heightAt(x, z - d);
    return glm::normalize(Vec3(-dx, 2.0f * d, -dz));
}

AABB Heightfield::bounds() const noexcept
{
    return AABB{Vec3(m_ref.firstSample.x, m_ref.minY, m_ref.firstSample.y),
                Vec3(m_ref.firstSample.x + static_cast<f32>(m_ref.width - 1) * m_ref.cellSize, m_ref.maxY,
                     m_ref.firstSample.y + static_cast<f32>(m_ref.height - 1) * m_ref.cellSize)};
}

render::HeightfieldDesc Heightfield::renderDesc() const noexcept
{
    return {m_ref.width, m_ref.height, m_ref.cellSize, m_ref.firstSample, m_ref.minY, m_ref.maxY, m_samples};
}
} // namespace g7::world

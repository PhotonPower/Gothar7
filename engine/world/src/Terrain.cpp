#include <g7/asset/Vfs.hpp>
#include <g7/world/Terrain.hpp>

#include <algorithm>
#include <cmath>
#include <format>

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
    return create(ref, std::move(samples));
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

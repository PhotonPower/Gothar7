#include <g7/runtime/MenuFlight.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace g7
{
namespace
{
Vec3 catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, f32 t)
{
    const f32 t2 = t * t;
    const f32 t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

std::optional<Vec3> vec3(const Config& config, const std::string& key)
{
    const auto values = config.find<std::vector<f64>>(key);
    if (!values || values->size() != 3)
    {
        return std::nullopt;
    }
    return Vec3(static_cast<f32>((*values)[0]), static_cast<f32>((*values)[1]),
                static_cast<f32>((*values)[2]));
}
} // namespace

Result<MenuFlight> MenuFlight::fromConfig(const Config& config)
{
    if (config.get<i64>("version", 0) != 1)
    {
        return Error{"menu flight: version must be 1"};
    }
    std::vector<MenuFlightPoint> points;
    for (usize i = 0; i < config.arraySize("point"); ++i)
    {
        const auto position = vec3(config, std::format("point[{}].position", i));
        const auto look = vec3(config, std::format("point[{}].look", i));
        if (!position || !look)
        {
            return Error{std::format("menu flight: point {} needs position and look as [x, y, z]", i + 1)};
        }
        points.push_back({*position, *look});
    }
    if (points.size() < 2)
    {
        return Error{"menu flight: at least two points"};
    }
    return MenuFlight(std::move(points), static_cast<f32>(config.get<f64>("speed", 3.0)),
                      config.get<bool>("loop", true));
}

MenuFlight::MenuFlight(std::vector<MenuFlightPoint> points, f32 speed, bool loop)
    : m_points(std::move(points)), m_loop(loop)
{
    const f32 metresPerSecond = std::max(speed, 0.1f);
    const usize segments = m_points.size() < 2 ? 0 : (m_loop ? m_points.size() : m_points.size() - 1);
    for (usize i = 0; i < segments; ++i)
    {
        const MenuFlightPoint& a = m_points[i];
        const MenuFlightPoint& b = m_points[(i + 1) % m_points.size()];
        // The longer of the two ways decides, so a turn on the spot does not whip round.
        const f32 metres =
            std::max(glm::length(b.position - a.position), glm::length(b.look - a.look) * 0.5f);
        m_segmentSeconds.push_back(std::max(metres / metresPerSecond, 1.0f));
        m_total += m_segmentSeconds.back();
    }
}

MenuFlightPose MenuFlight::at(f32 seconds) const
{
    if (m_points.empty())
    {
        return {};
    }
    if (empty() || m_total <= 0.0f)
    {
        return {m_points.front().position, m_points.front().look};
    }
    f32 t = m_loop ? std::fmod(std::max(seconds, 0.0f), m_total) : std::clamp(seconds, 0.0f, m_total);
    usize segment = 0;
    while (segment + 1 < m_segmentSeconds.size() && t > m_segmentSeconds[segment])
    {
        t -= m_segmentSeconds[segment];
        ++segment;
    }
    const f32 u = std::clamp(t / m_segmentSeconds[segment], 0.0f, 1.0f);
    const auto n = static_cast<long long>(m_points.size());
    const auto point = [&](long long i) -> const MenuFlightPoint&
    {
        if (m_loop)
        {
            return m_points[static_cast<usize>((i % n + n) % n)];
        }
        return m_points[static_cast<usize>(std::clamp(i, 0LL, n - 1))];
    };
    const auto i = static_cast<long long>(segment);
    return {
        catmullRom(point(i - 1).position, point(i).position, point(i + 1).position, point(i + 2).position, u),
        catmullRom(point(i - 1).look, point(i).look, point(i + 1).look, point(i + 2).look, u)};
}
} // namespace g7

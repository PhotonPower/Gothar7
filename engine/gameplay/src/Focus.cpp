#include <g7/core/Config.hpp>
#include <g7/gameplay/Focus.hpp>

#include <cmath>
#include <format>

namespace g7::gameplay
{
namespace
{
constexpr f32 kDegrees = 57.2957795f;

/// Horizontal angle in degrees between `direction` and the way to `point` (an item at the feet is as much
/// "ahead" as one at eye height), and the distance to it in 3D.
std::pair<f32, f32> angleAndDistance(const Vec3& eye, const Vec3& direction, const Vec3& point)
{
    const Vec3 to = point - eye;
    const f32 distance = glm::length(to);
    const Vec3 flat(to.x, 0.0f, to.z);
    const Vec3 flatDirection(direction.x, 0.0f, direction.z);
    if (glm::length(flat) < 1e-4f || glm::length(flatDirection) < 1e-6f)
    {
        return {0.0f, distance}; // straight above or below
    }
    const f32 cosine = std::clamp(glm::dot(glm::normalize(flat), glm::normalize(flatDirection)), -1.0f, 1.0f);
    return {std::acos(cosine) * kDegrees, distance};
}
} // namespace

std::string_view focusKindName(FocusKind kind) noexcept
{
    switch (kind)
    {
    case FocusKind::Npc:
        return "npc";
    case FocusKind::Mob:
        return "mob";
    case FocusKind::Item:
    case FocusKind::Count:
        break;
    }
    return "item";
}

Result<FocusSettings> FocusSettings::parse(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    FocusSettings s;
    const auto positive = [&](const std::string& key, f32& value) -> Result<void>
    {
        if (!c.contains(key))
        {
            return {};
        }
        const auto number = c.find<f64>(key);
        if (!number || !std::isfinite(*number) || *number <= 0.0)
        {
            return Error{std::format("{}: '{}' must be a positive number", source, key)};
        }
        value = static_cast<f32>(*number);
        return {};
    };
    for (usize k = 0; k < s.ranges.size(); ++k)
    {
        const std::string table(focusKindName(static_cast<FocusKind>(k)));
        for (const auto& [key, value] : {std::pair{table + ".distance", &s.ranges[k].distance},
                                         std::pair{table + ".angle", &s.ranges[k].angle}})
        {
            if (auto ok = positive(key, *value); !ok)
            {
                return ok.error();
            }
        }
        if (s.ranges[k].angle >= 90.0f)
        {
            return Error{std::format("{}: '{}.angle' must be below 90 degrees", source, table)};
        }
    }
    for (const auto& [key, value] : {std::pair{"keep", &s.keep}, std::pair{"height", &s.height}})
    {
        if (auto ok = positive(key, *value); !ok)
        {
            return ok.error();
        }
    }
    if (s.keep < 1.0f)
    {
        return Error{std::format("{}: 'keep' must be at least 1", source)};
    }
    return s;
}

std::optional<u64> selectFocus(std::span<const FocusCandidate> candidates, const Vec3& eye,
                               const Vec3& viewDirection, const FocusSettings& settings,
                               std::optional<u64> current, const FocusVisible& visible)
{
    const f32 length = glm::length(viewDirection);
    if (length < 1e-6f)
    {
        return std::nullopt;
    }
    const Vec3 direction = viewDirection / length;
    const auto within = [&](const FocusCandidate& c, f32 widen)
    {
        const FocusRange& range = settings.range(c.kind);
        const auto [angle, distance] = angleAndDistance(eye, direction, c.point);
        return angle <= range.angle * widen && distance <= range.distance * widen &&
               std::abs(c.point.y - eye.y) <= settings.height * widen;
    };

    // Best per kind: angle and distance as shares of their range (0 = straight ahead and close).
    const FocusCandidate* best = nullptr;
    f32 bestScore = 0.0f;
    for (const FocusCandidate& c : candidates)
    {
        if (!within(c, 1.0f))
        {
            continue;
        }
        const FocusRange& range = settings.range(c.kind);
        const auto [angle, distance] = angleAndDistance(eye, direction, c.point);
        const f32 score = angle / range.angle + 0.5f * distance / range.distance;
        const bool better =
            best == nullptr || c.kind < best->kind || (c.kind == best->kind && score < bestScore);
        if (better && (!visible || visible(c)))
        {
            best = &c;
            bestScore = score;
        }
    }
    // Hysteresis: the current focus stays inside its widened range unless a higher kind came into range.
    if (current)
    {
        for (const FocusCandidate& c : candidates)
        {
            if (c.id == *current && within(c, settings.keep) && (best == nullptr || best->kind >= c.kind) &&
                (!visible || visible(c)))
            {
                return c.id;
            }
        }
    }
    return best != nullptr ? std::optional<u64>(best->id) : std::nullopt;
}
} // namespace g7::gameplay

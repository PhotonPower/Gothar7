#include <g7/runtime/StartView.hpp>

#include <charconv>
#include <cmath>
#include <format>

namespace g7
{
Result<f32> parseNumber(std::string_view text)
{
    f32 value = 0.0f;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value))
    {
        return Error{std::format("'{}' is not a number", text)};
    }
    return value;
}

Result<Vec3> parseVec3(std::string_view text)
{
    const std::string_view whole = text;
    Vec3 out(0.0f);
    for (int i = 0; i < 3; ++i)
    {
        const usize comma = text.find(',');
        if ((i < 2) != (comma != std::string_view::npos))
        {
            return Error{std::format("expected x,y,z, got '{}'", whole)};
        }
        auto number = parseNumber(i < 2 ? text.substr(0, comma) : text);
        if (!number)
        {
            return number.error();
        }
        out[i] = number.value();
        text = i < 2 ? text.substr(comma + 1) : std::string_view();
    }
    return out;
}

Result<bool> parseStartViewArgument(std::string_view arg, StartView& view)
{
    const auto value = [&](std::string_view prefix) { return arg.substr(prefix.size()); };
    const auto bad = [&](const Error& error) { return Error{std::format("{}: {}", arg, error.message)}; };
    if (arg == "--fly")
    {
        view.fly = true;
        return true;
    }
    for (const auto& [prefix, target] : {std::pair{std::string_view("--cam="), &view.camera},
                                         std::pair{std::string_view("--player="), &view.player}})
    {
        if (arg.starts_with(prefix))
        {
            auto v = parseVec3(value(prefix));
            if (!v)
            {
                return bad(v.error());
            }
            *target = v.value();
            return true;
        }
    }
    for (const auto& [prefix, target] : {std::pair{std::string_view("--yaw="), &view.yawDegrees},
                                         std::pair{std::string_view("--pitch="), &view.pitchDegrees}})
    {
        if (arg.starts_with(prefix))
        {
            auto v = parseNumber(value(prefix));
            if (!v)
            {
                return bad(v.error());
            }
            *target = v.value();
            return true;
        }
    }
    return false;
}

std::string formatViewLine(const ViewLine& line)
{
    std::string out;
    if (!line.world.empty())
    {
        const bool quote = line.world.find(' ') != std::string::npos;
        out = std::format("--world={}{}{} ", quote ? "\"" : "", line.world, quote ? "\"" : "");
    }
    f32 yaw = std::remainder(line.yawDegrees, 360.0f); // [-180, 180]
    yaw = yaw <= -180.0f ? yaw + 360.0f : yaw;
    // Round half away from zero first, so -0.0 never shows as "-0.0".
    const auto fixed = [](f32 v, f32 scale) { return std::round(v * scale) / scale + 0.0f; };
    const Vec3& p = line.position;
    if (line.fly)
    {
        out += std::format("--cam={:.2f},{:.2f},{:.2f} --yaw={:.1f} --pitch={:.1f}", fixed(p.x, 100.0f),
                           fixed(p.y, 100.0f), fixed(p.z, 100.0f), fixed(yaw, 10.0f),
                           fixed(line.pitchDegrees, 10.0f));
    }
    else
    {
        out += std::format("--player={:.2f},{:.2f},{:.2f} --yaw={:.1f}", fixed(p.x, 100.0f),
                           fixed(p.y, 100.0f), fixed(p.z, 100.0f), fixed(yaw, 10.0f));
    }
    out += std::format(" --time={:02}:{:02}", (line.minuteOfDay / 60) % 24, line.minuteOfDay % 60);
    if (line.fly)
    {
        out += " --fly";
    }
    return out;
}
} // namespace g7

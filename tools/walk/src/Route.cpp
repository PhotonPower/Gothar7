#include <g7/walk/Route.hpp>

#include <nlohmann/json.hpp>

#include <format>

namespace g7::walk
{
namespace
{
using Json = nlohmann::json;

Error fail(std::string_view source, std::string_view where, std::string_view what)
{
    return Error{std::format("{}: {}{}{}", source, where, where.empty() ? "" : ": ", what)};
}

Result<Gait> readGait(const Json& j, std::string_view source, std::string_view where)
{
    const std::string g = j.is_string() ? j.get<std::string>() : std::string();
    if (g == "run")
    {
        return Gait::Run;
    }
    if (g == "walk")
    {
        return Gait::Walk;
    }
    if (g == "sneak")
    {
        return Gait::Sneak;
    }
    return fail(source, where, "'gait' must be 'run', 'walk' or 'sneak'");
}

bool positiveNumber(const Json& j)
{
    return j.is_number() && j.get<f64>() > 0.0;
}
} // namespace

std::string_view gaitName(Gait gait) noexcept
{
    return gait == Gait::Walk ? "walk" : gait == Gait::Sneak ? "sneak" : "run";
}

Result<Route> parseRoute(std::string_view json, std::string_view source)
{
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return fail(source, "", "not a JSON object");
    }
    if (root.value("version", 0) != 1)
    {
        return fail(source, "", "needs \"version\": 1");
    }
    Route route;
    if (root.contains("start"))
    {
        if (!root["start"].is_string())
        {
            return fail(source, "", "'start' must be a start point name");
        }
        route.start = root["start"].get<std::string>();
    }
    if (root.contains("gait"))
    {
        auto gait = readGait(root["gait"], source, "");
        if (!gait)
        {
            return gait.error();
        }
        route.gait = gait.value();
    }
    if (root.contains("time"))
    {
        if (!root["time"].is_string())
        {
            return fail(source, "", "'time' must be \"HH:MM\"");
        }
        route.time = root["time"].get<std::string>();
    }
    if (root.contains("timeLimit"))
    {
        if (!positiveNumber(root["timeLimit"]))
        {
            return fail(source, "", "'timeLimit' must be a positive number of seconds");
        }
        route.timeLimit = root["timeLimit"].get<f64>();
    }
    if (root.contains("screenshot"))
    {
        if (root["screenshot"] != "all")
        {
            return fail(source, "", "route-level 'screenshot' can only be \"all\"");
        }
        route.screenshotAll = true;
    }
    if (root.contains("screenshotEveryM"))
    {
        if (!positiveNumber(root["screenshotEveryM"]))
        {
            return fail(source, "", "'screenshotEveryM' must be a positive number");
        }
        route.screenshotEveryM = root["screenshotEveryM"].get<f32>();
    }
    if (!root.contains("points") || !root["points"].is_array() || root["points"].empty())
    {
        return fail(source, "", "needs a non-empty 'points' array");
    }
    for (usize i = 0; i < root["points"].size(); ++i)
    {
        const Json& p = root["points"][i];
        const std::string where = std::format("points[{}]", i);
        if (!p.is_object())
        {
            return fail(source, where, "must be an object");
        }
        RoutePoint point;
        point.name = p.value("name", std::format("P{:03}", i + 1));
        if (!p.contains("pos") || !p["pos"].is_array() || p["pos"].size() != 2 || !p["pos"][0].is_number() ||
            !p["pos"][1].is_number())
        {
            return fail(source, where, "needs 'pos': [x, z]");
        }
        point.pos = Vec2(p["pos"][0].get<f32>(), p["pos"][1].get<f32>());
        if (p.contains("y"))
        {
            if (!p["y"].is_number())
            {
                return fail(source, where, "'y' must be a number");
            }
            point.y = p["y"].get<f32>();
        }
        if (p.contains("radius"))
        {
            if (!positiveNumber(p["radius"]))
            {
                return fail(source, where, "'radius' must be positive");
            }
            point.radius = p["radius"].get<f32>();
        }
        if (p.contains("gait"))
        {
            auto gait = readGait(p["gait"], source, where);
            if (!gait)
            {
                return gait.error();
            }
            point.gait = gait.value();
        }
        if (p.contains("action"))
        {
            const std::string action =
                p["action"].is_string() ? p["action"].get<std::string>() : std::string();
            if (action == "jump")
            {
                point.action = RoutePoint::Action::Jump;
            }
            else if (action == "climb")
            {
                point.action = RoutePoint::Action::Climb;
            }
            else
            {
                return fail(source, where, "'action' must be 'jump' or 'climb'");
            }
        }
        point.screenshot = p.value("screenshot", false) || route.screenshotAll;
        point.teleport = p.value("teleport", false);
        route.points.push_back(std::move(point));
    }
    return route;
}
} // namespace g7::walk

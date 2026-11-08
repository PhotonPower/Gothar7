#include <g7/core/Config.hpp>
#include <g7/perf/Scenario.hpp>

#include <algorithm>
#include <charconv>
#include <format>

namespace g7::perf
{
namespace
{
Result<Vec3> vec3(const Config& c, const std::string& key, std::string_view source)
{
    const auto values = c.find<std::vector<f64>>(key);
    if (!values || values->size() != 3)
    {
        return Error{std::format("{}: {} needs three numbers [x, y, z]", source, key)};
    }
    return Vec3(static_cast<f32>((*values)[0]), static_cast<f32>((*values)[1]),
                static_cast<f32>((*values)[2]));
}
} // namespace

Result<u32> parseClock(std::string_view text)
{
    const usize colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0 || text.size() - colon != 3)
    {
        return Error{std::format("time '{}' is not hh:mm", text)};
    }
    u32 hour = 0;
    u32 minute = 0;
    const auto h = std::from_chars(text.data(), text.data() + colon, hour);
    const auto m = std::from_chars(text.data() + colon + 1, text.data() + text.size(), minute);
    if (h.ec != std::errc{} || h.ptr != text.data() + colon || m.ec != std::errc{} ||
        m.ptr != text.data() + text.size() || hour > 23 || minute > 59)
    {
        return Error{std::format("time '{}' is not hh:mm", text)};
    }
    return hour * 60 + minute;
}

Result<PerfFile> parsePerfFile(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    PerfFile file;
    file.warmupSeconds = c.get<f64>("warmup_seconds", file.warmupSeconds);
    const i64 frames = c.get<i64>("frames", file.frames);
    if (frames < 1 || file.warmupSeconds < 0.0)
    {
        return Error{std::format("{}: frames must be >= 1 and warmup_seconds >= 0", source)};
    }
    file.frames = static_cast<u32>(frames);
    for (usize i = 0, n = c.arraySize("scenario"); i < n; ++i)
    {
        const std::string at = std::format("scenario[{}]", i);
        Scenario s;
        s.name = c.get<std::string>(at + ".name", "");
        if (s.name.empty() || s.name == "all" || s.name.find(',') != std::string::npos)
        {
            return Error{std::format("{}: {} needs a 'name' (not 'all', no commas)", source, at)};
        }
        if (std::any_of(file.scenarios.begin(), file.scenarios.end(),
                        [&](const Scenario& other) { return other.name == s.name; }))
        {
            return Error{std::format("{}: scenario '{}' twice", source, s.name)};
        }
        s.description = c.get<std::string>(at + ".description", "");
        auto camera = vec3(c, at + ".cam", source);
        if (!camera)
        {
            return camera.error();
        }
        s.camera = camera.value();
        s.yawDegrees = static_cast<f32>(c.get<f64>(at + ".yaw", 0.0));
        s.pitchDegrees = static_cast<f32>(c.get<f64>(at + ".pitch", 0.0));
        s.player = s.camera - Vec3(0.0f, 1.6f, 0.0f);
        if (c.contains(at + ".player"))
        {
            auto player = vec3(c, at + ".player", source);
            if (!player)
            {
                return player.error();
            }
            s.player = player.value();
        }
        for (const std::string& time : c.get<std::vector<std::string>>(at + ".times", {"10:00"}))
        {
            auto minute = parseClock(time);
            if (!minute)
            {
                return Error{std::format("{}: {}: {}", source, at, minute.error().message)};
            }
            s.minutesOfDay.push_back(minute.value());
        }
        if (s.minutesOfDay.empty())
        {
            return Error{std::format("{}: {} needs at least one time", source, at)};
        }
        const i64 extra = c.get<i64>(at + ".extra_npcs", 0);
        s.npcs = c.get<std::vector<std::string>>(at + ".npcs", {});
        s.npcRadius = static_cast<f32>(c.get<f64>(at + ".npc_radius", s.npcRadius));
        if (extra < 0 || (extra > 0 && s.npcs.empty()) || s.npcRadius <= 0.0f)
        {
            return Error{std::format("{}: {}: extra_npcs >= 0 needs 'npcs' and npc_radius > 0", source, at)};
        }
        s.extraNpcs = static_cast<u32>(extra);
        file.scenarios.push_back(std::move(s));
    }
    return file;
}

Result<std::vector<Scenario>> selectScenarios(const PerfFile& file, std::string_view names)
{
    std::vector<Scenario> selected;
    if (names == "all")
    {
        // The stress scenarios last: their extra NPCs stay in the world.
        selected = file.scenarios;
        std::stable_partition(selected.begin(), selected.end(),
                              [](const Scenario& s) { return s.extraNpcs == 0; });
        return selected;
    }
    while (!names.empty())
    {
        const usize comma = names.find(',');
        const std::string_view name = names.substr(0, comma);
        const auto it = std::find_if(file.scenarios.begin(), file.scenarios.end(),
                                     [&](const Scenario& s) { return s.name == name; });
        if (it == file.scenarios.end())
        {
            std::string known;
            for (const Scenario& s : file.scenarios)
            {
                known += (known.empty() ? "" : ", ") + s.name;
            }
            return Error{std::format("no perf scenario '{}' (known: {}, all)", name, known)};
        }
        selected.push_back(*it);
        names = comma == std::string_view::npos ? std::string_view() : names.substr(comma + 1);
    }
    if (selected.empty())
    {
        return Error{"no perf scenario given"};
    }
    return selected;
}
} // namespace g7::perf

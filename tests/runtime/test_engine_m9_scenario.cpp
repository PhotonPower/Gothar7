// M9 milestone (DoD, docs/03-roadmap.md): ten NPCs follow their routines through a whole game day without an
// error, and the guard reacts to a drawn weapon and to the player in his area within a few ticks (label
// "headless"). The day runs fast (a game minute in a quarter second) with the player far away, so the NPCs
// are not simulated and jump to the places of their routines (AI LOD); then two hours with the player among
// them.

#include <g7/core/Config.hpp>
#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;

namespace
{
constexpr std::array<const char*, 10> kNames = {
    "npc_gate_guard", "npc_farmer_woman", "npc_woodcutter", "npc_old_man",   "mon_wolf",
    "mon_wolf#2",     "mon_wolf#3",       "mon_keiler",     "mon_laufvogel", "mon_laufvogel#2"};

/// For every NPC: the state its routine wants now, if it has another one ("" if all agree).
constexpr std::string_view kCheck = R"(
function m9_check(names)
    local t = where().time
    local now = tonumber(t:sub(1, 2)) * 60 + tonumber(t:sub(4, 5))
    local function minute(text)
        return tonumber(text:sub(1, 2)) * 60 + tonumber(text:sub(4, 5))
    end
    local wrong = {}
    for _, npc in ipairs(names) do
        local s = npc_state(npc)
        local want = nil
        for _, e in ipairs(instance("Routine", s.routine)) do
            local from, to = minute(e.from), minute(e.to) % 1440
            if (from < to and now >= from and now < to) or (from >= to and (now >= from or now < to)) then
                want = e
            end
        end
        if not want or s.state ~= want.state or s.at ~= want.at then
            wrong[#wrong + 1] = string.format("%s at %s: %s/%s, wants %s/%s", npc, t, s.state, s.at,
                want and want.state or "?", want and want.at or "?")
        end
    end
    return table.concat(wrong, "; ")
end
)";

EngineConfig scenarioConfig()
{
    EngineConfig config;
    config.appName = "m9";
    config.headless = true;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = "START_LAGER";
    config.startTime = "06:30"; // checks at half past: never on an entry's border
    config.fixedFrameSeconds = 1.0 / 60.0;
    auto settings = Config::parse("[time]\nminute_seconds = 0.25\n");
    REQUIRE(settings.ok());
    config.settings = std::move(settings).value();
    return config;
}

script::Value run(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    const std::string what = std::string(line) + ": " + (result.ok() ? "" : result.error().message);
    REQUIRE_MESSAGE(result.ok(), what);
    return result.value();
}

void runSeconds(Engine& engine, f32 seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

std::string names()
{
    std::string list = "{";
    for (const char* n : kNames)
    {
        list += std::format("'{}',", n);
    }
    return list + "}";
}
} // namespace

TEST_CASE("M9 scenario: ten NPCs follow their routines through a game day, the guard reacts at once")
{
    Engine engine(scenarioConfig());
    auto result = engine.init();
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    run(engine, kCheck);
    run(engine, "camp_people()");
    run(engine, "insert_pack('mon_wolf', 3)");
    run(engine, "insert_animal('mon_keiler') insert_animal('mon_laufvogel') insert_animal('mon_laufvogel')");
    for (const char* n : kNames)
    {
        CHECK_MESSAGE(!run(engine, std::format("npc_state('{}').routine", n)).asString().empty(), n);
    }
    const u64 errorsBefore = engine.scripts()->callErrors();

    // A whole day with the player far away: each hour every NPC in the state of its routine entry, standing
    // at its place (they jump there; a state begins only when the player comes near).
    run(engine, "teleport(400, 0, 400)");
    for (int hour = 0; hour < 24; ++hour)
    {
        runSeconds(engine, 15.0f); // 60 game minutes
        const std::string wrong(run(engine, std::format("m9_check({})", names())).asString());
        CHECK_MESSAGE(wrong.empty(), wrong);
        for (const char* n : kNames)
        {
            const std::string at(run(engine, std::format("npc_state('{}').at", n)).asString());
            const auto point = engine.waynet().find(at);
            REQUIRE_MESSAGE(point.has_value(), at);
            const Vec3 p(static_cast<f32>(run(engine, std::format("npc_state('{}').x", n)).asNumber()), 0.0f,
                         static_cast<f32>(run(engine, std::format("npc_state('{}').z", n)).asNumber()));
            const Vec3 wp = engine.waynet().points()[*point].position;
            CHECK_MESSAGE(glm::length(Vec2(p.x - wp.x, p.z - wp.z)) < 1.0f, n, " at ", at);
        }
    }

    // Two hours among them (the camp at noon, the animals' meadows nearby): simulated, still on their
    // routines.
    run(engine, "time(11, 30)");
    run(engine, "teleport(5, 0, 2)");
    for (int i = 0; i < 2; ++i)
    {
        runSeconds(engine, 15.0f);
        const std::string wrong(run(engine, std::format("m9_check({})", names())).asString());
        // Animals near the player may threaten or flee from each other; people near him must keep their
        // routine.
        for (const char* n : {"npc_gate_guard", "npc_farmer_woman", "npc_woodcutter", "npc_old_man"})
        {
            CHECK_MESSAGE(wrong.find(n) == std::string::npos, wrong);
        }
    }
    CHECK(engine.scripts()->callErrors() == errorsBefore); // no script error all day

    // The guard at his gate (21.5, 0, 0): a weapon drawn in his sight - he warns within 30 ticks (half a
    // second).
    run(engine, "teleport(28, 0, 1)");
    run(engine, "npc_clear('npc_gate_guard') npc_turn_to_player('npc_gate_guard')");
    runSeconds(engine, 2.0f);
    REQUIRE(run(engine, "npc_sees_player('npc_gate_guard')").asBool());
    run(engine, "draw_weapon()");
    int ticks = 0;
    while (ticks < 120 && run(engine, "npc_state('npc_gate_guard').state").asString() != "zs_warn_weapon")
    {
        REQUIRE(engine.runFrame());
        ++ticks;
    }
    CHECK(ticks <= 30);
    run(engine, "draw_weapon()");
    runSeconds(engine, 6.0f);

    // His sleeping place: the player walks in - he notices within 30 ticks.
    run(engine, "npc_clear('npc_gate_guard') npc_turn_to_player('npc_gate_guard')");
    runSeconds(engine, 1.0f);
    run(engine, "teleport(28.5, 0, -3)");
    ticks = 0;
    while (ticks < 120 && run(engine, "npc_state('npc_gate_guard').state").asString() != "zs_intruder")
    {
        REQUIRE(engine.runFrame());
        ++ticks;
    }
    CHECK(ticks <= 30);
    CHECK(engine.scripts()->callErrors() == errorsBefore);
}

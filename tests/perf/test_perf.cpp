#include <g7/core/FileSystem.hpp>
#include <g7/perf/Report.hpp>
#include <g7/perf/Scenario.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace g7;
using namespace g7::perf;

namespace
{
constexpr std::string_view kTwo = R"(
warmup_seconds = 2.5
frames = 100

[[scenario]]
name = "stress"
cam = [0, 2, 0]
extra_npcs = 3
npcs = ["npc_a"]

[[scenario]]
name = "plaza"
description = "test"
cam = [1, 2, 3]
yaw = 30
pitch = -5
player = [1, 0, 3]
times = ["10:00", "21:30"]
)";
} // namespace

TEST_CASE("Perf: clock times")
{
    CHECK(parseClock("10:00").value() == 600);
    CHECK(parseClock("21:30").value() == 1290);
    CHECK(parseClock("0:05").value() == 5);
    CHECK_FALSE(parseClock("24:00"));
    CHECK_FALSE(parseClock("10:60"));
    CHECK_FALSE(parseClock("10"));
    CHECK_FALSE(parseClock("10:0"));
    CHECK_FALSE(parseClock("a0:00"));
    CHECK(clockText(1290) == "21:30");
    CHECK(clockText(5) == "00:05");
}

TEST_CASE("Perf: perf.toml")
{
    auto file = parsePerfFile(kTwo, "test");
    REQUIRE(file);
    CHECK(file.value().warmupSeconds == doctest::Approx(2.5));
    CHECK(file.value().frames == 100);
    REQUIRE(file.value().scenarios.size() == 2);
    const Scenario& stress = file.value().scenarios[0];
    CHECK(stress.player.y == doctest::Approx(0.4f)); // 1.6 m below the camera
    CHECK(stress.minutesOfDay == std::vector<u32>{600});
    CHECK(stress.extraNpcs == 3);
    const Scenario& plaza = file.value().scenarios[1];
    CHECK(plaza.description == "test");
    CHECK(plaza.camera.z == doctest::Approx(3.0f));
    CHECK(plaza.yawDegrees == doctest::Approx(30.0f));
    CHECK(plaza.pitchDegrees == doctest::Approx(-5.0f));
    CHECK(plaza.player.y == doctest::Approx(0.0f));
    CHECK(plaza.minutesOfDay == std::vector<u32>{600, 1290});

    SUBCASE("selection")
    {
        auto all = selectScenarios(file.value(), "all");
        REQUIRE(all);
        REQUIRE(all.value().size() == 2);
        CHECK(all.value()[0].name == "plaza"); // the stress one last
        CHECK(all.value()[1].name == "stress");
        auto some = selectScenarios(file.value(), "stress,plaza");
        REQUIRE(some);
        CHECK(some.value()[0].name == "stress");
        auto unknown = selectScenarios(file.value(), "plaza,markt");
        REQUIRE_FALSE(unknown);
        CHECK(unknown.error().message.find("known: stress, plaza") != std::string::npos);
        CHECK_FALSE(selectScenarios(file.value(), ""));
    }
}

TEST_CASE("Perf: perf.toml errors")
{
    CHECK_FALSE(parsePerfFile("frames = 0", "t"));
    CHECK_FALSE(parsePerfFile("[[scenario]]\ncam = [0, 0, 0]", "t"));                 // no name
    CHECK_FALSE(parsePerfFile("[[scenario]]\nname = \"all\"\ncam = [0, 0, 0]", "t")); // reserved
    CHECK_FALSE(parsePerfFile("[[scenario]]\nname = \"a\"\ncam = [0, 0]", "t"));      // two numbers
    CHECK_FALSE(parsePerfFile("[[scenario]]\nname = \"a\"\ncam = [0, 0, 0]\ntimes = [\"25:00\"]", "t"));
    CHECK_FALSE(parsePerfFile("[[scenario]]\nname = \"a\"\ncam = [0, 0, 0]\nextra_npcs = 2", "t")); // no npcs
    CHECK_FALSE(parsePerfFile("[[scenario]]\nname = \"a\"\ncam = [0, 0, 0]\n[[scenario]]\nname = \"a\"\n"
                              "cam = [0, 0, 0]",
                              "t")); // twice
}

TEST_CASE("Perf: the game's perf.toml")
{
    auto text = fs::readFile(fs::fromUtf8(G7_ASSET_SOURCE_DIR "/data/perf.toml"));
    REQUIRE(text);
    auto file = parsePerfFile(
        std::string_view(reinterpret_cast<const char*>(text.value().data()), text.value().size()),
        "data/perf.toml");
    REQUIRE(file);
    CHECK(file.value().frames == 600);
    CHECK(file.value().warmupSeconds == doctest::Approx(5.0));
    for (const char* name : {"markt", "gasse", "stube", "markt_voll"})
    {
        CAPTURE(name);
        auto one = selectScenarios(file.value(), name);
        REQUIRE(one);
        if (std::string_view(name) == "markt_voll")
        {
            CHECK(one.value()[0].extraNpcs == 60);
        }
        else
        {
            CHECK(one.value()[0].minutesOfDay == std::vector<u32>{600, 1260});
        }
    }
}

TEST_CASE("Perf: report")
{
    Report report;
    report.scenario = "markt";
    report.build = "0.1.0";
    report.renderer = "GPU";
    report.width = 1600;
    report.height = 900;
    RunResult run;
    run.minuteOfDay = 600;
    run.frame.frames = 600;
    run.frame.averageMs = 8.0;
    run.frame.p50Ms = 7.5;
    run.zones.push_back({"physics", {600, 0.5, 0.4, 0.8, 1.0, 2.0}, 1.0});
    run.gpu.push_back({"shadow", 1.5});
    run.gpu.push_back({"main", 4.0});
    run.drawCalls = 800.0;
    run.triangles = 1.5e6;
    run.lights = 12.0;
    run.creatures = 42;
    run.simulatedCreatures = 18.5;
    run.simulatedMax = 20;
    report.runs.push_back(run);
    report.loadPhases.push_back({"world", 120.0, 300.0});

    const std::string json = toJson(report);
    CHECK(json.find("\"scenario\": \"markt\"") != std::string::npos);
    CHECK(json.find("\"time\": \"10:00\"") != std::string::npos);
    CHECK(json.find("\"p50_ms\": 7.5") != std::string::npos);
    CHECK(json.find("\"name\": \"physics\"") != std::string::npos);
    CHECK(json.find("\"gpu_total_ms\": 5.5") != std::string::npos);
    CHECK(json.find("\"npcs_simulated_max\": 20") != std::string::npos);
    CHECK(json.find("\"end_ms\": 300.0") != std::string::npos);

    const std::string line = summaryLine("markt", run);
    CHECK(line.starts_with("perf markt 10:00: 600 frames"));
    CHECK(line.find("GPU 5.50 ms") != std::string::npos);
    CHECK(line.find("18.5/42 NPCs simulated") != std::string::npos);
}

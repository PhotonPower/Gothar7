// The autopilot end to end (label "gpu"): a route through the test camp - climbing, a fall, swimming,
// a closed gate - with the log and the summary it writes.

#include "GlFixture.hpp"

#include <g7/runtime/Engine.hpp>
#include <g7/walk/Autopilot.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace g7;

TEST_CASE("Autopilot GPU: a route through the camp, logged")
{
    auto route = walk::parseRoute(R"({
      "version": 1, "start": "START_KLETTERPLATZ", "timeLimit": 120,
      "points": [
        {"name": "P01_VOR_BLOCK", "pos": [40.0, 3.8], "gait": "walk", "screenshot": true},
        {"name": "P02_AUF_BLOCK", "pos": [40.0, 6.0], "action": "climb", "radius": 0.6},
        {"name": "P03_TREPPE_OBEN", "pos": [50.0, 12.0], "teleport": true, "y": 6.0},
        {"name": "P04_RUNTER", "pos": [50.0, 16.0]},
        {"name": "P05_TEICH_UFER", "pos": [-124.0, -120.0], "teleport": true},
        {"name": "P06_SCHWIMMEN", "pos": [-145.0, -120.0]},
        {"name": "P07_ZAUN", "pos": [30.0, 0.0], "teleport": true},
        {"name": "P08_DURCHS_TOR", "pos": [20.0, 0.0]}
      ]})",
                                  "camp.json");
    REQUIRE_MESSAGE(route.ok(), (route.ok() ? "" : route.error().message));

    const fs::Path out = std::filesystem::temp_directory_path() / "g7_walk_test";
    std::error_code ec;
    std::filesystem::remove_all(out, ec);
    EngineConfig config;
    config.appName = "walk";
    config.window.size = {160, 90};
    config.window.vsync = false;
    config.world = fs::fromUtf8("testworld/camp.g7world");
    config.start = route.value().start;
    config.shaderDirectory = fs::fromUtf8(G7_SHADER_DIR);
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.maxFrames = 60 * 120;
    g7::test::keepVideoAlive();
    Engine engine(std::move(config));
    REQUIRE(engine.init().ok());
    walk::Autopilot autopilot(std::move(route).value(), out);
    engine.addTool(autopilot);
    while (engine.runFrame())
    {
    }
    CHECK(autopilot.finished());

    std::ifstream logFile(out / "walk.jsonl");
    std::stringstream log;
    log << logFile.rdbuf();
    const std::string text = log.str();
    CHECK(text.find(R"("event":"start")") != std::string::npos);
    CHECK(text.find(R"("event":"climb")") != std::string::npos);
    CHECK(text.find(R"("event":"fall","t")") != std::string::npos);
    CHECK(text.find(R"("height":6.0)") != std::string::npos); // from the 6 m step
    CHECK(text.find(R"("event":"swim_start")") != std::string::npos);
    CHECK(text.find(R"("vob":"fence-gate")") != std::string::npos); // stuck at the closed gate
    CHECK(std::filesystem::exists(out / "P01_VOR_BLOCK.png"));

    std::ifstream summaryFile(out / "walk_summary.json");
    std::stringstream summary;
    summary << summaryFile.rdbuf();
    CHECK(summary.str().find(R"("reached": 7)") != std::string::npos);
    CHECK(summary.str().find(R"("stuck": 1)") != std::string::npos);
    CHECK(summary.str().find("P08_DURCHS_TOR") != std::string::npos);
    std::filesystem::remove_all(out, ec);
}

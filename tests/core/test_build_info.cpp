// Version and build info (M17, docs/modules/release.md part A): the lines the log, the main menu, --version
// and save games show; the generated info agrees with the engine's version.

#include <g7/core/BuildInfo.hpp>
#include <g7/core/Version.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream>
#include <string>

using namespace g7;

TEST_CASE("Build info: the version and build lines")
{
    const BuildInfo clean{"0.3.0", "abc1234", false, "release", "2026-11-07"};
    CHECK(versionText(clean) == "0.3.0 (abc1234)");
    CHECK(buildLine(clean) == "Gothar 0.3.0 (abc1234, release)");
    const BuildInfo dirty{"0.3.0", "abc1234", true, "debug", ""};
    CHECK(versionText(dirty) == "0.3.0 (abc1234+)");
    CHECK(buildLine(dirty) == "Gothar 0.3.0 (abc1234+, debug)");
}

TEST_CASE("Build info: generated for this build, the same version as the engine")
{
    const BuildInfo& info = buildInfo();
    CHECK(info.version == std::format("{}.{}.{}", kEngineVersion.major, kEngineVersion.minor,
                                      kEngineVersion.patch)); // CMake project
    CHECK((info.gitHash == "unknown" || info.gitHash.size() == 7));
    CHECK_FALSE(info.buildType.empty());
    CHECK(buildLine().starts_with("Gothar " + std::string(info.version) + " ("));
}

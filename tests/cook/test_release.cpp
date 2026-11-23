// The release's notices (M17, release.md part C): every library vcpkg.json brings has a line in
// THIRD_PARTY_NOTICES.txt, so no library comes into the game without its licence named.

#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <regex>
#include <set>
#include <string>

using namespace g7;

TEST_CASE("Release: THIRD_PARTY_NOTICES.txt names every library of vcpkg.json")
{
    const fs::Path root = fs::fromUtf8(G7_ASSET_SOURCE_DIR).parent_path().parent_path();
    auto manifest = fs::readText(root / "vcpkg.json");
    auto notices = fs::readText(root / "tools" / "release" / "THIRD_PARTY_NOTICES.txt");
    REQUIRE(manifest.ok());
    REQUIRE(notices.ok());
    // The dependencies: plain strings and {"name": ...} entries.
    std::set<std::string> ports;
    const std::regex named(R"re("name"\s*:\s*"([a-z0-9-]+)")re");
    const std::regex plain(R"re(^\s*"([a-z0-9-]+)",?\s*$)re");
    for (auto it = std::sregex_iterator(manifest.value().begin(), manifest.value().end(), named);
         it != std::sregex_iterator(); ++it)
    {
        ports.insert((*it)[1].str());
    }
    std::string line;
    for (const char c : manifest.value() + "\n")
    {
        if (c != '\n')
        {
            line += c;
            continue;
        }
        if (std::smatch m; std::regex_match(line, m, plain))
        {
            ports.insert(m[1].str());
        }
        line.clear();
    }
    ports.erase("gothar"); // the manifest's own name
    ports.erase("x11");    // SDL's Linux features
    ports.erase("wayland");
    CHECK(ports.size() > 10);
    for (const std::string& port : ports)
    {
        CAPTURE(port);
        CHECK(notices.value().find("(" + port + ")") != std::string::npos);
    }
}

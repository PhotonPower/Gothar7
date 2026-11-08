// The pure package test (M17, release.md part C): the built ZIP (environment G7_PACKAGE, set by
// tools/release/make_release.cmake) is unpacked into a folder of the test run - nowhere else - and the game
// starts from there with nothing of the repository: the camp with its NPCs and figures comes from data.g7pak.
// Without a package the test has nothing to check and passes.

#include "RunTempDir.hpp"

#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <ostream>
#include <string>

using namespace g7;

namespace
{
int run(const std::string& command)
{
#ifdef _WIN32
    return std::system(("call " + command).c_str()); // "call": cmd keeps the quotes of paths with spaces
#else
    return std::system(command.c_str());
#endif
}
} // namespace

TEST_CASE("Package: unpacked into a folder of its own, the game starts from the package alone")
{
    const char* package = std::getenv("G7_PACKAGE");
    if (package == nullptr || *package == '\0')
    {
        MESSAGE("no package (G7_PACKAGE unset) - built by tools/release/make_release.cmake");
        return;
    }
    const std::filesystem::path dir = test::runTempDir() / "g7_package";
    std::filesystem::create_directories(dir);
    // No shell "cd": cmake changes into the folder itself.
    const std::string cmake = fs::toUtf8(std::filesystem::path(G7_CMAKE).make_preferred()); // cmd: no "/"
    const std::string zip = fs::toUtf8(std::filesystem::absolute(fs::fromUtf8(package)));
    REQUIRE(run(std::format("\"{0}\" -E chdir \"{1}\" \"{0}\" -E tar xf \"{2}\"", cmake, fs::toUtf8(dir),
                            zip)) == 0);
    std::filesystem::path game;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_directory() && fs::toUtf8(entry.path().filename()).starts_with("Gothar-"))
        {
            game = entry.path();
        }
    }
    REQUIRE_FALSE(game.empty());
    for (const char* file :
         {"data/data.g7pak", "config/engine.toml", "CREDITS.txt", "THIRD_PARTY_NOTICES.txt", "README.txt",
          "LICENSES/ASSETS.md", "LICENSES/GOTHAR.txt", "LICENSES/OFL.txt", "scripts/startup.lua", "shaders"})
    {
        CAPTURE(file);
        CHECK(std::filesystem::exists(game / file));
    }
#ifdef _WIN32
    const std::filesystem::path exe = (game / "gothar.exe").make_preferred();
#else
    const std::filesystem::path exe = game / "gothar";
#endif
    REQUIRE(std::filesystem::exists(exe));
    const std::filesystem::path log = dir / "run.log";
    const int rc = run(
        std::format("\"{}\" --smoke-test --no-user-config --user-dir=\"{}\" --crash-dir=\"{}\" "
                    "--world=testworld/camp.g7world --exec=camp_people() > \"{}\" 2>&1",
                    fs::toUtf8(exe), fs::toUtf8(dir / "user"), fs::toUtf8(dir / "crashes"), fs::toUtf8(log)));
    const auto read = fs::readText(log);
    const std::string text = read ? read.value() : std::string();
    CHECK(rc == 0);
    CHECK(text.find("data.g7pak") != std::string::npos); // the archive was mounted
    CHECK(text.find("ERROR") == std::string::npos);
    CHECK(text.find("assets\\source") == std::string::npos); // nothing from the repository
    CHECK(text.find("assets/source") == std::string::npos);
    // Figures and clips come from the archive too (plan M17 part 0): a missing part, set or image is only a
    // warning in the game and a failing insert_npc only a script error - both fail here.
    const bool missing = text.find("cannot load") != std::string::npos ||
                         text.find("not found in the VFS") != std::string::npos;
    const bool scriptError =
        text.find("[sol2]") != std::string::npos ||
        text.find("--exec camp_people():") != std::string::npos; // logged on failure only
    usize figures = 0;
    for (usize at = text.find(": 60 bones, "); at != std::string::npos;
         at = text.find(": 60 bones, ", at + 1))
    {
        ++figures;
    }
    CHECK_FALSE(missing);
    CHECK_FALSE(scriptError);
    CHECK(figures >= 7); // the hero and the six people of camp_people()
    if (rc != 0 || text.find("ERROR") != std::string::npos || missing || scriptError || figures < 7)
    {
        MESSAGE(text);
    }
}

#include <g7/platform/Paths.hpp>

#include <doctest/doctest.h>

#include <filesystem>

using namespace g7;

TEST_CASE("userDataDirectory: exists and is writable")
{
    auto dir = platform::userDataDirectory("Gothar", "GotharTests");
    REQUIRE_MESSAGE(dir.ok(), (dir.ok() ? "" : dir.error().message));
    const fs::Path path = dir.value();

    CHECK(std::filesystem::is_directory(path));
    CHECK(path.filename() == "GotharTests"); // no trailing separator

    const fs::Path probe = path / "probe.txt";
    CHECK(fs::writeTextAtomic(probe, "ok").ok());
    auto text = fs::readText(probe);
    REQUIRE(text.ok());
    CHECK(text.value() == "ok");

    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

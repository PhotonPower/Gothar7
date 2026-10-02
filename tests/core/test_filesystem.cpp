#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

namespace
{
/// Fresh directory per test case, removed on scope exit.
class TempDir
{
public:
    TempDir()
    {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("g7_fs_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(m_path);
    }
    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const g7::fs::Path& path() const { return m_path; }

private:
    g7::fs::Path m_path;
};
} // namespace

TEST_CASE("fs: binary round trip keeps every byte")
{
    TempDir dir;
    const auto file = dir.path() / "data.bin";
    const std::vector<g7::u8> data = {0x00, 0x01, 0xFF, 0x0A, 0x0D, 0x00, 0x7F};

    REQUIRE(g7::fs::writeFile(file, data).ok());
    auto read = g7::fs::readFile(file);
    REQUIRE(read.ok());
    CHECK(read.value() == data);
}

TEST_CASE("fs: text round trip without newline conversion")
{
    TempDir dir;
    const auto file = dir.path() / "text.txt";
    const std::string text = "line1\nline2\r\nend";

    REQUIRE(g7::fs::writeText(file, text).ok());
    auto read = g7::fs::readText(file);
    REQUIRE(read.ok());
    CHECK(read.value() == text);
}

TEST_CASE("fs: empty file")
{
    TempDir dir;
    const auto file = dir.path() / "empty";

    REQUIRE(g7::fs::writeFile(file, {}).ok());
    auto read = g7::fs::readFile(file);
    REQUIRE(read.ok());
    CHECK(read.value().empty());
}

TEST_CASE("fs: missing file reports path in error")
{
    TempDir dir;
    auto read = g7::fs::readText(dir.path() / "does_not_exist.txt");
    REQUIRE_FALSE(read.ok());
    CHECK(read.error().message.find("does_not_exist.txt") != std::string::npos);
}

TEST_CASE("fs: writing into missing directory fails")
{
    TempDir dir;
    auto result = g7::fs::writeText(dir.path() / "no" / "such" / "dir.txt", "x");
    CHECK_FALSE(result.ok());
}

TEST_CASE("fs: atomic write replaces target and leaves no temp file")
{
    TempDir dir;
    const auto file = dir.path() / "save.dat";

    REQUIRE(g7::fs::writeTextAtomic(file, "first").ok());
    REQUIRE(g7::fs::writeTextAtomic(file, "second, longer").ok());

    auto read = g7::fs::readText(file);
    REQUIRE(read.ok());
    CHECK(read.value() == "second, longer");

    int entries = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(dir.path()))
    {
        ++entries;
    }
    CHECK(entries == 1);
}

TEST_CASE("fs: atomic write into missing directory fails cleanly")
{
    TempDir dir;
    const std::vector<g7::u8> data = {1, 2, 3};
    CHECK_FALSE(g7::fs::writeFileAtomic(dir.path() / "missing" / "save.dat", data).ok());
}

TEST_CASE("fs: UTF-8 file names")
{
    TempDir dir;
    const auto file = dir.path() / g7::fs::fromUtf8("Lagerfeuer_\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F.txt");

    REQUIRE(g7::fs::writeText(file, "warm").ok());
    CHECK(g7::fs::exists(file));
    CHECK(g7::fs::toUtf8(file.filename()) == "Lagerfeuer_\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F.txt");
    auto read = g7::fs::readText(file);
    REQUIRE(read.ok());
    CHECK(read.value() == "warm");
}

TEST_CASE("fs: createDirectories creates nested directories and is idempotent")
{
    TempDir dir;
    const auto nested = dir.path() / "a" / "b" / "c";

    REQUIRE(g7::fs::createDirectories(nested).ok());
    CHECK(std::filesystem::is_directory(nested));
    CHECK(g7::fs::createDirectories(nested).ok());
}

TEST_CASE("fs: createDirectories fails when a file is in the way")
{
    TempDir dir;
    const auto file = dir.path() / "blocker";
    REQUIRE(g7::fs::writeText(file, "x").ok());
    CHECK_FALSE(g7::fs::createDirectories(file / "sub").ok());
}

TEST_CASE("fs: game and user paths resolve against base directories")
{
    const auto previous = g7::fs::baseDirectories();

    g7::fs::setBaseDirectories({.gameDir = g7::fs::Path("game_root"), .userDir = g7::fs::Path("user_root")});
    CHECK(g7::fs::gamePath("assets/cooked/world.g7pak") ==
          (g7::fs::Path("game_root") / "assets" / "cooked" / "world.g7pak").lexically_normal());
    CHECK(g7::fs::userPath("saves/../config.toml") ==
          (g7::fs::Path("user_root") / "config.toml").lexically_normal());
    CHECK(g7::fs::baseDirectories().gameDir == g7::fs::Path("game_root"));

    g7::fs::setBaseDirectories(previous);
}

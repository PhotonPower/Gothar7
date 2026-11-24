// The credits ("Mitwirkende", release.md decision 10, W7): from assets/licenses.toml and
// THIRD_PARTY_NOTICES.txt, required credits LGL and OSM first, every asset source with its licences, the
// thanks and the libraries, in German and English; the main menu opens them.

#include "RunTempDir.hpp"

#include <g7/core/FileSystem.hpp>
#include <g7/runtime/Credits.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/JournalScreens.hpp>
#include <g7/ui/MenuScreens.hpp>

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <filesystem>
#include <ostream>
#include <string>

using namespace g7;

namespace
{
std::string repoText(const char* relative)
{
    auto text = fs::readText(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / ".." / ".." / fs::fromUtf8(relative));
    REQUIRE(text.ok());
    return text.value();
}

const CreditsHeadings kHeadings{"[required]", "[sources]", "[thanks]", "[libraries]"};
} // namespace

TEST_CASE(
    "Credits: required credits by order (LGL, OSM first), all sources, thanks, libraries; German and English")
{
    const std::string toml = repoText("assets/licenses.toml");
    const std::string notices = repoText("tools/release/THIRD_PARTY_NOTICES.txt");
    auto de = parseCredits(toml, notices, "de");
    REQUIRE(de.ok());
    CHECK(de.value().sources.size() >= 9);
    CHECK(de.value().libraries.size() >= 15);
    CHECK(std::ranges::none_of(de.value().libraries,
                               [](const std::string& l) { return l.starts_with("doctest"); }));
    const std::string text = creditsScreenText(de.value(), kHeadings);
    const usize lgl = text.find("LGL, www.lgl-bw.de");
    const usize osm = text.find("© OpenStreetMap-Mitwirkende");
    REQUIRE(lgl != std::string::npos);
    REQUIRE(osm != std::string::npos);
    CHECK(lgl < osm);
    CHECK(osm < text.find("Alegreya"));
    CHECK(lgl > text.find("[required]"));
    CHECK(osm < text.find("[sources]"));
    // Every source with its licences, the thanks, the libraries.
    for (const CreditSource& s : de.value().sources)
    {
        CHECK(text.find("- " + s.name + ": ") != std::string::npos);
        CHECK_FALSE(s.licences.empty());
    }
    CHECK(text.find("Kenney (kenney.nl)") > text.find("[thanks]"));
    CHECK(text.find("SDL 3 (sdl3)") > text.find("[libraries]"));
    CHECK(text.find("Datenlizenz Deutschland") != std::string::npos);

    auto en = parseCredits(toml, notices, "en");
    REQUIRE(en.ok());
    const std::string english = creditsScreenText(en.value(), kHeadings);
    CHECK(english.find("© OpenStreetMap contributors") != std::string::npos);
    CHECK(english.find("data source: LGL, www.lgl-bw.de") != std::string::npos);
    CHECK(english.find("Data licence Germany") != std::string::npos);
    CHECK(english.find("Mitwirkende") == std::string::npos);

    CHECK_FALSE(parseCredits("version = 2", notices, "de").ok());
    CHECK_FALSE(parseCredits("version = 1\n[[source]]\nlicences = [\"own\"]\n", "", "de").ok());
}

TEST_CASE("Credits: the main menu opens them, action closes them")
{
    auto settings = Config::load(fs::fromUtf8(G7_GAME_CONFIG));
    REQUIRE(settings.ok());
    Config merged = settings.value();
    merged.set<std::string>("menu.world", "testworld/camp.g7world");
    merged.set<std::string>("menu.flight", "");
    merged.set<std::string>("ui.language", "en");
    EngineConfig config;
    config.window.size = {640, 360};
    config.render = false;
    config.fixedFrameSeconds = 1.0 / 60.0;
    config.mainMenu = true;
    config.defaultSettings = settings.value();
    config.settings = merged;
    config.saveDirectory = g7::test::runTempDir() / "g7_credits_saves";
    Engine engine(config);
    REQUIRE(engine.init().ok());
    for (int i = 0; i < 300 && engine.screens().find("menu") == nullptr; ++i)
    {
        REQUIRE(engine.runFrame());
    }
    auto* menu = dynamic_cast<ui::MenuScreen*>(engine.screens().find("menu"));
    REQUIRE(menu != nullptr);
    menu->select("credits");
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    event.key.down = true;
    REQUIRE(SDL_PushEvent(&event));
    REQUIRE(engine.runFrame());
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    REQUIRE(SDL_PushEvent(&event));
    REQUIRE(engine.runFrame());
    auto* credits = dynamic_cast<ui::CreditsScreen*>(engine.screens().find("credits"));
    REQUIRE(credits != nullptr);
    CHECK(credits->text().starts_with("Gothar - a game"));
    CHECK(credits->text().find("Required credits") != std::string::npos);
    CHECK(credits->text().find("© OpenStreetMap contributors") != std::string::npos);
    CHECK(credits->text().find("Libraries") != std::string::npos);
    CHECK(engine.creditsText() == credits->text());
}

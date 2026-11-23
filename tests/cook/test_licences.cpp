// Licence check of a release (M17, release.md part C): patterns, coverage, kinds that may not be shipped,
// credit lines, LICENSES.md and licenses.toml in step - and the repository's own assets pass.

#include "Licences.hpp"

#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::cook;

namespace
{
const char* const kTable = R"(version = 1
[licence.own]
name = "eigene Arbeit"
redistributable = true
[licence.CC0-1_0]
name = "CC0 1.0"
redistributable = true
[licence.DL-DE-BY-2_0]
name = "Datenlizenz Deutschland"
redistributable = true
attribution = true
[licence.CMU-MoCap]
name = "CMU"
redistributable = true
attribution = true
[licence.no-resale]
name = "only for research"
redistributable = false

[[source]]
name = "CMU"
licences = ["CMU-MoCap"]
credit = "The data used in this project was obtained from mocap.cs.cmu.edu."
credit_order = 2
md = "CMU Graphics Lab"
paths = ["characters/anims/human/fist.*"]

[[source]]
name = "LGL"
licences = ["DL-DE-BY-2_0"]
credit = "Datengrundlage: LGL"
credit_order = 1
md = "LGL"
paths = ["worlds/leonberg/**"]

[[source]]
name = "Research only"
licences = ["no-resale"]
md = "research"
optional_md = true
paths = ["secret/*"]

[[source]]
name = "Kenney"
licences = ["CC0-1_0"]
thanks = "Kenney"
md = "Kenney"
paths = ["characters/**", "testscene/**"]

[[source]]
name = "Own"
licences = ["own"]
md = "eigene Arbeit"
paths = ["data/*.toml"]
)";

const char* const kMd = R"(# Lizenzen
| Quelle | Lizenz | Pflichtangabe |
|---|---|---|
| LGL | Datenlizenz Deutschland – Namensnennung 2.0 | „Datengrundlage: LGL“ |

| Asset | Quelle | Lizenz | Pflichtangabe |
|---|---|---|---|
| fist | CMU Graphics Lab Motion Capture Database | Nutzungsbedingungen von CMU, wörtlich: „This data is free for use in research projects.“ | Danksagung |
| town | Kenney | CC0 1.0 | keine |
| data | eigene Arbeit | wie das Repository | keine |
)";
} // namespace

TEST_CASE("Licences: patterns - * within a folder name, ** across folders")
{
    CHECK(globMatch("characters/**", "characters/anims/human/fist.glb"));
    CHECK(globMatch("characters/anims/human/fist.*", "characters/anims/human/fist.glb"));
    CHECK_FALSE(globMatch("characters/anims/human/fist.*", "characters/anims/human/fist/x.glb"));
    CHECK(globMatch("data/*.toml", "data/mobs.toml"));
    CHECK_FALSE(globMatch("data/*.toml", "data/anim/human.toml"));
    CHECK(globMatch("worlds/leonberg/generated/*splat*", "worlds/leonberg/generated/leonberg_splat0.png"));
    CHECK(globMatch("**", "a/b/c"));
    CHECK_FALSE(globMatch("testscene/*", "testscene/town/a.glb"));
}

TEST_CASE("Licences: coverage, kinds that may not be shipped, credits in order")
{
    auto table = parseLicences(kTable, "test");
    REQUIRE(table.ok());
    CHECK(table.value().sources.size() == 5);
    const std::vector<std::string> files{"characters/anims/human/fist.glb",
                                         "characters/figures/a.glb",
                                         "worlds/leonberg/generated/terrain.r16",
                                         "data/mobs.toml",
                                         "secret/data.bvh",
                                         "unknown/file.png"};
    const LicenceReport report = checkLicences(table.value(), files, kMd);
    CHECK(report.uncovered == std::vector<std::string>{"unknown/file.png"});
    REQUIRE(report.forbidden.size() == 1);
    CHECK(report.forbidden[0].starts_with("secret/data.bvh"));
    CHECK(report.tableErrors.empty());
    CHECK_FALSE(report.ok());
    CHECK(report.used.at(0) == 1); // the first match: fist is CMU, not Kenney's characters/**
    const std::string credits = creditsText(table.value(), report, "Gothar 0.3.0 (abc1234)");
    CHECK(credits.starts_with("Gothar 0.3.0 (abc1234)\n"));
    CHECK(credits.find("Datengrundlage: LGL") < credits.find("mocap.cs.cmu.edu")); // credit_order
    CHECK(credits.find("- Kenney") != std::string::npos);
}

TEST_CASE("Licences: the table must match LICENSES.md - unknown licences, missing credits and rows")
{
    CHECK(licenceKindOf("CC0 1.0 (Lizenztext `town/License.txt`)") == "CC0-1_0");
    CHECK(licenceKindOf("eigene Arbeit des Projektinhabers") == "owner");
    CHECK(licenceKindOf("wie das Repository") == "own");
    CHECK(licenceKindOf("SIL Open Font License 1.1") == "OFL-1_1");
    CHECK(licenceKindOf("Nutzungsbedingungen von CMU, wörtlich: „This data is free …“") == "CMU-MoCap");
    CHECK(licenceKindOf("Datenlizenz Deutschland – Namensnennung 2.0") == "DL-DE-BY-2_0");
    CHECK(licenceKindOf("ODbL 1.0") == "ODbL-1_0");
    CHECK(licenceKindOf("Public Domain") == "public-domain");
    CHECK(licenceKindOf("GPL 3").empty());
    CHECK(markdownLicences(kMd).size() == 4);

    auto table = parseLicences(kTable, "test");
    REQUIRE(table.ok());
    LicenceTable broken = table.value();
    broken.sources[0].credit.clear();             // CMU asks for a credit line
    broken.sources[1].licences.push_back("nope"); // unknown kind
    broken.sources[4].md = "not in the list";
    const std::string md =
        std::string(kMd) + "\n| Asset | Quelle | Lizenz |\n|---|---|---|\n| x | y | GPL 3 |\n";
    const LicenceReport report = checkLicences(broken, {}, md);
    const auto has = [&](std::string_view text)
    {
        return std::ranges::any_of(report.tableErrors,
                                   [&](const std::string& e) { return e.find(text) != std::string::npos; });
    };
    CHECK(has("CMU: its licence asks for a credit line"));
    CHECK(has("unknown licence 'nope'"));
    CHECK(has("'not in the list' is not in LICENSES.md"));
    CHECK(has("\"GPL 3\" has no kind"));
    CHECK_FALSE(has("Research only")); // optional_md: its row may still be missing
}

TEST_CASE("Licences: the repository's assets pass (every file has a source, every licence a kind)")
{
    const fs::Path assets = fs::fromUtf8(G7_ASSET_SOURCE_DIR).parent_path();
    auto toml = fs::readText(assets / "licenses.toml");
    auto md = fs::readText(assets / "LICENSES.md");
    REQUIRE(toml.ok());
    REQUIRE(md.ok());
    auto table = parseLicences(toml.value(), "licenses.toml");
    REQUIRE(table.ok());
    CHECK(markdownLicences(md.value()).size() > 30);
    const LicenceReport report = checkLicences(table.value(), assetFiles(assets / "source"), md.value());
    for (const std::string& e : report.tableErrors)
    {
        MESSAGE(e);
    }
    for (const std::string& f : report.uncovered)
    {
        MESSAGE("no source: " << f);
    }
    CHECK(report.ok());
    CHECK(table.value().kinds.contains("CMU-MoCap"));
}

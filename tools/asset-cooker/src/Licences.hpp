#pragma once

// Licence check of a release (M17, docs/modules/release.md part C): every asset file needs a source in
// assets/licenses.toml whose licences may be shipped; the sources used give the release's CREDITS.txt.

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace g7::cook
{
struct LicenceKind
{
    std::string id; ///< "CC0-1_0"
    std::string name;
    bool redistributable = false;
    bool attribution = false; ///< a credit line is required
};

struct LicenceSource
{
    std::string name;
    std::vector<std::string> licences; ///< kind ids
    std::vector<std::string> paths;    ///< patterns relative to assets/source
    std::string credit;                ///< required credit line (for kinds with attribution)
    std::string thanks;                ///< voluntary mention
    std::string md;                    ///< phrase that must stand in LICENSES.md
    bool optionalMd = false;           ///< the LICENSES.md row may still be missing (a pending PR)
    i64 creditOrder = 100;             ///< position of the credit line in CREDITS.txt (smaller first)
};

struct LicenceTable
{
    std::map<std::string, LicenceKind, std::less<>> kinds;
    std::vector<LicenceSource> sources; ///< in file order: the first match wins
};

/// Reads assets/licenses.toml (version 1). Errors name the entry.
[[nodiscard]] Result<LicenceTable> parseLicences(std::string_view toml, std::string_view sourceName);

/// `*` matches within a path segment, `**` across segments (also none); `/` separates.
[[nodiscard]] bool globMatch(std::string_view pattern, std::string_view path) noexcept;

struct LicenceReport
{
    std::vector<std::string> uncovered;   ///< files without a source
    std::vector<std::string> forbidden;   ///< "file: licence" whose kind may not be shipped
    std::vector<std::string> tableErrors; ///< unknown kinds, a credit missing, md phrase not in LICENSES.md
    std::map<usize, usize> used;          ///< source index -> number of files

    [[nodiscard]] bool ok() const noexcept
    {
        return uncovered.empty() && forbidden.empty() && tableErrors.empty();
    }
};

/// Checks `files` (paths relative to assets/source, '/' separated) against the table and LICENSES.md.
[[nodiscard]] LicenceReport checkLicences(const LicenceTable& table, const std::vector<std::string>& files,
                                          std::string_view licensesMd);

/// The licence kind a text of LICENSES.md's "Lizenz" column names ("CC0 1.0 (Lizenztext ...)" -> "CC0-1_0");
/// empty if none is recognised.
[[nodiscard]] std::string licenceKindOf(std::string_view text);
/// Every value of a "Lizenz" column in the tables of LICENSES.md (one entry per row), in order.
[[nodiscard]] std::vector<std::string> markdownLicences(std::string_view licensesMd);

/// CREDITS.txt: the required credit lines of the sources used, then the voluntary thanks.
[[nodiscard]] std::string creditsText(const LicenceTable& table, const LicenceReport& report,
                                      std::string_view versionLine);

/// The files a release takes from `source` (as the cooker walks it: no hidden files), relative and sorted.
[[nodiscard]] std::vector<std::string> assetFiles(const fs::Path& source);
} // namespace g7::cook

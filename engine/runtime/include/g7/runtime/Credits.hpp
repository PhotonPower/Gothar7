#pragma once

// The credits screen of the main menu (W7 / release.md decision 10): the required credits (LGL and OSM
// first), every asset source with its licences and the thanks from assets/licenses.toml, the libraries from
// THIRD_PARTY_NOTICES.txt - in German or English.

#include <g7/core/Result.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace g7
{
/// One source of assets/licenses.toml as the credits show it.
struct CreditSource
{
    std::string name;
    std::vector<std::string> licences; ///< the licence kinds' names in the language
    std::string credit;                ///< required credit line (empty: none)
    std::string thanks;                ///< voluntary mention (empty: none)
    i64 creditOrder = 100;
};

struct CreditsData
{
    std::vector<CreditSource> sources;  ///< in file order
    std::vector<std::string> libraries; ///< "SDL 3 (sdl3) - zlib License - ..." (those in the game)
};

/// Reads licenses.toml (version 1) and THIRD_PARTY_NOTICES.txt. `language` "en" takes `credit_en`,
/// `thanks_en` and the kinds' `name_en` where given; otherwise (and for "de") the German texts.
[[nodiscard]] Result<CreditsData> parseCredits(std::string_view licensesToml, std::string_view notices,
                                               std::string_view language);

/// The section headings (from the UI texts).
struct CreditsHeadings
{
    std::string required;  ///< "Pflichtangaben"
    std::string sources;   ///< "Assets und Lizenzen"
    std::string thanks;    ///< "Dank an"
    std::string libraries; ///< "Bibliotheken"
};

/// The screen's text: required credits by credit_order, every source with its licences, the thanks, the
/// libraries.
[[nodiscard]] std::string creditsScreenText(const CreditsData& data, const CreditsHeadings& headings);
} // namespace g7

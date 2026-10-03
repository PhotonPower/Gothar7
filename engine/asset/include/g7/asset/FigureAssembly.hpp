#pragma once

// Figures assembled from parts at run time (M6 part D2): the C++ version of `gothar-chargen assemble`
// (characters-pipeline.md §6.2), so heads, outfits and armour can be swapped in the game. Same algorithm,
// same data (the parts' asset.extras.gothar); a test compares it with the Python result.

#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace g7::asset
{
/// `figures/<name>.figure.toml`, format v1: which parts make up a figure and their colours.
struct FigureManifest
{
    struct Part
    {
        std::string role; ///< "body", "head", "hair", "beard" or "cloth_<piece>"
        std::string path; ///< relative to the characters folder, e.g. "parts/body_m_average/body.glb"
    };
    /// body, head, hair, beard (those present), then the garments in list order.
    std::vector<Part> parts;
    /// Material name -> linear RGB (the manifest has sRGB "#rrggbb").
    std::vector<std::pair<std::string, Vec3>> palette;

    [[nodiscard]] static Result<FigureManifest> parse(std::string_view toml, std::string_view source);

    [[nodiscard]] const Part* find(std::string_view role) const noexcept;
    /// Sets (or adds) a part; an empty path removes it. body and head cannot be removed.
    Result<void> setPart(std::string_view role, std::string_view path);
    /// Replaces all garments ("cloth" list), in this order.
    void setCloth(std::span<const std::string> paths);
    [[nodiscard]] std::vector<std::string> cloth() const;
};

/// "parts/cloth_m_average/elvs_crude_t-shirt_male.glb" -> "cloth_elvs_crude_t_shirt_male".
[[nodiscard]] std::string clothRole(std::string_view path);

/// A loaded part for assembleFigure.
struct FigurePart
{
    std::string role;
    std::string path; ///< VFS path of the .glb: its images are resolved against it
    const SkinnedModelData* data = nullptr;
};

/// Builds the figure (skeleton from the body, parts as `<role>_lod<n>`, body triangles under garments
/// removed, the neck snapped onto the head, materials merged by name with the head's skin first, palette
/// colours, roles hidden by headgear dropped). Image URIs become VFS paths. Errors: missing body/head or
/// their neck data, garments fitted to another body, rings that do not match, a neck more than 6 mm off.
[[nodiscard]] Result<SkinnedModelData> assembleFigure(const FigureManifest& manifest,
                                                      std::span<const FigurePart> parts);
} // namespace g7::asset

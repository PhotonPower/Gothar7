#pragma once

// Mip chains of RGBA8 images on the CPU - for the cooker's KTX2 files and for alpha-tested textures uploaded
// uncooked - and alpha coverage kept through them: an alpha-tested surface (beard stubble, hair, leaves)
// keeps the share of its texels above the cutoff in every level, instead of thinning out with distance
// (Castaño 2010, "Computing alpha mipmaps"; figuren's alpha MASK beards).

#include <g7/asset/ImageData.hpp>

#include <span>
#include <vector>

namespace g7::asset
{
enum class MipFilter : u8
{
    Srgb,   ///< colour: averaged in linear light, stored as sRGB; alpha linear
    Linear, ///< four independent linear channels
    Normal, ///< a tangent-space normal map: averaged and renormalised
};

/// Half the size (at least 1 x 1), 2 x 2 box filter.
[[nodiscard]] ImageData halveImage(const ImageData& image, MipFilter filter);
/// The complete chain down to 1 x 1, level 0 the image itself.
[[nodiscard]] std::vector<ImageData> buildMipChain(const ImageData& image, MipFilter filter);

/// Share of the texels whose alpha times `scale` reaches `cutoff` (0 .. 1).
[[nodiscard]] f32 alphaCoverage(const ImageData& image, f32 cutoff, f32 scale = 1.0f) noexcept;
/// Scales the alpha of levels 1 .. n so each covers as much at `cutoff` as level 0.
void preserveAlphaCoverage(std::span<ImageData> chain, f32 cutoff);
} // namespace g7::asset

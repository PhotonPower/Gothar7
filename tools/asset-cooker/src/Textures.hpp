#pragma once

// Texture cooking for g7-cook (ADR 0016): mip chains and KTX2/UASTC encoding.

#include <g7/asset/ImageData.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <vector>

namespace g7::cook
{
enum class TextureUsage : u8
{
    Color,  ///< sRGB colour (+ alpha) -> BC7 sRGB at load time
    Normal, ///< tangent-space normal map -> two channels (X, Y), linear -> BC5 at load time
};

/// Complete mip chain (level 0 = the image itself) down to 1 x 1, box-filtered: colour is
/// averaged in linear light (alpha linearly), normals are averaged and renormalised.
[[nodiscard]] std::vector<asset::ImageData> buildMipChain(const asset::ImageData& image, TextureUsage usage);

/// Encodes an RGBA8 image as KTX2: full mip chain, UASTC (level 0 = fastest .. 4 = best) with
/// zstd supercompression. Colour is stored as sRGB RGBA, normal maps as linear RG (X, Y).
[[nodiscard]] Result<std::vector<u8>> encodeKtx2(const asset::ImageData& image, TextureUsage usage,
                                                 u32 uastcLevel = 2);

/// False in builds without libktx (nodeps preset).
[[nodiscard]] bool hasKtx2Encoder() noexcept;
} // namespace g7::cook

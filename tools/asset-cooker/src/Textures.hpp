#pragma once

// Texture cooking for g7-cook (ADR 0016): mip chains and KTX2/UASTC encoding.

#include <g7/asset/ImageData.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <optional>
#include <vector>

namespace g7::cook
{
enum class TextureUsage : u8
{
    Color,  ///< sRGB colour (+ alpha) -> BC7 sRGB at load time
    Normal, ///< tangent-space normal map -> two channels (X, Y), linear -> BC5 at load time
    Data,   ///< four independent linear channels (terrain splat weights) -> BC7 linear at load time;
            ///< alpha is a weight like the others: no premultiplying, nothing dropped at alpha 0
};

/// Complete mip chain (level 0 = the image itself) down to 1 x 1, box-filtered: colour is
/// averaged in linear light (alpha linearly), normals are averaged and renormalised, data channels
/// are averaged each on its own.
[[nodiscard]] std::vector<asset::ImageData> buildMipChain(const asset::ImageData& image, TextureUsage usage);

/// Encodes an RGBA8 image as KTX2: full mip chain, UASTC (level 0 = fastest .. 4 = best) with
/// zstd supercompression. Colour is stored as sRGB RGBA, normal maps as linear RG (X, Y), data
/// as linear RGBA. `alphaCutoff`: the colour texture of an alpha-tested material (glTF MASK) - its mips keep
/// the alpha coverage at that cutoff (asset::preserveAlphaCoverage).
[[nodiscard]] Result<std::vector<u8>> encodeKtx2(const asset::ImageData& image, TextureUsage usage,
                                                 u32 uastcLevel = 2, std::optional<f32> alphaCutoff = {});

/// False in builds without libktx (nodeps preset).
[[nodiscard]] bool hasKtx2Encoder() noexcept;
} // namespace g7::cook

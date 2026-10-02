#pragma once

#include <g7/asset/ImageData.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

namespace g7::render
{
class Device;

struct TextureUpload
{
    bool srgb = true;    ///< Colour data (base colour, emissive); false for data maps (normals, masks).
    bool mipmaps = true; ///< Full mip chain generated on the GPU.
};

/// Uploads a decoded image as RGBA8(_SRGB). Rows are uploaded in file order, so UV (0,0) samples
/// the image's top-left texel – matching glTF's UV convention without flipping.
[[nodiscard]] Result<rhi::Texture> createTexture(Device& device, const asset::ImageData& image,
                                                 TextureUpload options = {});

/// 1x1 texture of one colour (fallback when a material has no texture, or it failed to load).
[[nodiscard]] Result<rhi::Texture> createSolidTexture(Device& device, u8 r, u8 g, u8 b, u8 a,
                                                      bool srgb = true);
} // namespace g7::render

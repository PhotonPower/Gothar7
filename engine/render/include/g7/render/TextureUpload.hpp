#pragma once

#include <g7/asset/ImageData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Result.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <optional>
#include <span>

namespace g7::render
{
class Device;

struct TextureUpload
{
    bool srgb = true;    ///< Colour data (base colour, emissive); false for data maps (normals, masks).
    bool mipmaps = true; ///< Full mip chain generated on the GPU.
    /// The colour texture of an alpha-tested material: the chain is built on the CPU, every level keeping the
    /// alpha coverage at this cutoff (asset::preserveAlphaCoverage), so the surface does not thin out with
    /// distance.
    std::optional<f32> alphaCutoff;
};

/// Uploads a decoded image as RGBA8(_SRGB). Rows are uploaded in file order, so UV (0,0) samples
/// the image's top-left texel – matching glTF's UV convention without flipping.
[[nodiscard]] Result<rhi::Texture> createTexture(Device& device, const asset::ImageData& image,
                                                 TextureUpload options = {});

/// Uploads a texture as the asset system delivers it (ADR 0016):
/// - RGBA8 (PNG/JPEG during development): sRGB if `colour` (base colour, emissive), linear for data
///   maps; the mip chain is generated on the GPU unless the data brings more than one level;
/// - BC7 (cooked colour): sRGB as recorded in the data, every level uploaded;
/// - BC5 (cooked two-channel normal map): linear, every level uploaded.
/// `alphaCutoff` as in TextureUpload, for a single RGBA8 level (cooked chains bring theirs).
[[nodiscard]] Result<rhi::Texture> createTexture(Device& device, const asset::TextureData& data, bool colour,
                                                 std::optional<f32> alphaCutoff = {});

enum class TextureArrayUsage : u8
{
    Colour, ///< albedo: RGBA8 as sRGB, BC7 as recorded
    Data,   ///< weights, masks: always linear (the bytes are the values)
};

/// A 2D array texture from equally sized layers of the same format and mip count; differences are
/// errors that name the layer. Single RGBA8 levels get a generated mip chain.
[[nodiscard]] Result<rhi::Texture> createTextureArray(Device& device,
                                                      std::span<const asset::TextureData* const> layers,
                                                      TextureArrayUsage usage);

/// 1x1 texture of one colour (fallback when a material has no texture, or it failed to load).
[[nodiscard]] Result<rhi::Texture> createSolidTexture(Device& device, u8 r, u8 g, u8 b, u8 a,
                                                      bool srgb = true);
} // namespace g7::render

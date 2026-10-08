#pragma once

#include <g7/asset/ImageData.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace g7::asset
{
/// GPU-ready pixel formats (ADR 0016). BC7 and BC5 come from cooked KTX2 files; RGBA8 from
/// PNG/JPEG during development (mipmaps are then generated on the GPU).
enum class TextureFormat : u8
{
    RGBA8, ///< 4 bytes per pixel
    BC7,   ///< 16 bytes per 4 x 4 block; colour with alpha
    BC5,   ///< 16 bytes per 4 x 4 block; two channels (X, Y of a normal map; Z is rebuilt in the shader)
};

struct TextureLevel
{
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> data;
};

/// A texture as the renderer uploads it: format, colour space and one or more mip levels
/// (level 0 = full size). Cooked KTX2 textures carry the complete mip chain.
struct TextureData
{
    TextureFormat format = TextureFormat::RGBA8;
    bool srgb = false; ///< Colour data in sRGB (BC7/RGBA8); normal maps and BC5 are linear.
    std::vector<TextureLevel> levels;

    [[nodiscard]] u32 width() const noexcept { return levels.empty() ? 0 : levels[0].width; }
    [[nodiscard]] u32 height() const noexcept { return levels.empty() ? 0 : levels[0].height; }
};

/// Wraps a decoded image as one RGBA8 level (PNG/JPEG path).
[[nodiscard]] TextureData textureFromImage(ImageData image, bool srgb = true);

/// Reads a KTX2 file written by g7-cook: UASTC is transcoded to BC7 (colour) or BC5 (two-channel
/// data), uncompressed RGBA8 is taken as is; sRGB comes from the file's transfer function.
/// Fails if the build has no KTX2 support (nodeps preset).
[[nodiscard]] Result<TextureData> decodeKtx2(std::span<const u8> bytes,
                                             std::string_view debugName = "<memory>");

/// Level 0 of a KTX2 file as plain RGBA8 on the CPU (data looked up by game code: the terrain's splat weights
/// for footsteps, M13 E). Same support as decodeKtx2.
[[nodiscard]] Result<ImageData> decodeKtx2Rgba(std::span<const u8> bytes,
                                               std::string_view debugName = "<memory>");

/// True if decodeKtx2 is available in this build.
[[nodiscard]] bool hasKtx2Support() noexcept;
} // namespace g7::asset

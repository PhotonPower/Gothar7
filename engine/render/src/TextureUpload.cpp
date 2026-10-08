#include <g7/asset/ImageMips.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/TextureUpload.hpp>

#include <string>

namespace g7::render
{
namespace
{
/// GPU format for asset data. Colour RGBA8 is sRGB, BC7 as recorded; `forceLinear` reads every
/// format as plain data (splat weights: the bytes are the values, whatever the file says).
rhi::Format formatFor(const asset::TextureData& data, bool colour, bool forceLinear = false)
{
    switch (data.format)
    {
    case asset::TextureFormat::RGBA8:
        return colour && !forceLinear ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8;
    case asset::TextureFormat::BC7:
        return data.srgb && !forceLinear ? rhi::Format::BC7_SRGB : rhi::Format::BC7;
    case asset::TextureFormat::BC5:
        return rhi::Format::BC5;
    }
    return rhi::Format::RGBA8;
}

/// Checks the level sizes; true if the GPU must generate the mip chain (a single RGBA8 level).
Result<bool> checkLevels(const asset::TextureData& data)
{
    if (data.levels.empty() || data.width() == 0 || data.height() == 0)
    {
        return Error{"invalid texture data: no levels"};
    }
    const u32 fullChain = rhi::mipLevelCount(data.width(), data.height());
    if (data.levels.size() > fullChain)
    {
        return Error{"invalid texture data: " + std::to_string(data.levels.size()) + " levels for " +
                     std::to_string(data.width()) + "x" + std::to_string(data.height())};
    }
    for (usize level = 0; level < data.levels.size(); ++level)
    {
        const asset::TextureLevel& l = data.levels[level];
        if (l.width != rhi::mipSize(data.width(), static_cast<u32>(level)) ||
            l.height != rhi::mipSize(data.height(), static_cast<u32>(level)))
        {
            return Error{"invalid texture data: level " + std::to_string(level) + " has the wrong size"};
        }
    }
    return data.format == asset::TextureFormat::RGBA8 && data.levels.size() == 1;
}

/// An alpha-tested colour texture: the chain on the CPU, its alpha coverage kept in every level.
Result<rhi::Texture> createCoverageTexture(Device& device, const asset::ImageData& image, bool srgb,
                                           f32 cutoff)
{
    std::vector<asset::ImageData> chain =
        asset::buildMipChain(image, srgb ? asset::MipFilter::Srgb : asset::MipFilter::Linear);
    asset::preserveAlphaCoverage(chain, cutoff);
    auto texture =
        device.createTexture({image.width, image.height, srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8,
                              static_cast<u32>(chain.size())});
    if (!texture)
    {
        return texture.error();
    }
    for (usize level = 0; level < chain.size(); ++level)
    {
        if (auto upload = texture.value().upload(static_cast<u32>(level), chain[level].rgba8); !upload)
        {
            return upload.error();
        }
    }
    return texture;
}

Result<void> uploadLevels(rhi::Texture& texture, const asset::TextureData& data, u32 layer)
{
    for (usize level = 0; level < data.levels.size(); ++level)
    {
        if (auto upload = texture.upload(static_cast<u32>(level), data.levels[level].data, layer); !upload)
        {
            return upload.error();
        }
    }
    return {};
}
} // namespace

Result<rhi::Texture> createTexture(Device& device, const asset::ImageData& image, TextureUpload options)
{
    if (image.width == 0 || image.height == 0 ||
        image.rgba8.size() != static_cast<usize>(image.width) * image.height * 4)
    {
        return Error{"invalid image data"};
    }
    if (options.mipmaps && options.alphaCutoff)
    {
        return createCoverageTexture(device, image, options.srgb, *options.alphaCutoff);
    }
    auto texture = device.createTexture({image.width, image.height,
                                         options.srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8,
                                         options.mipmaps ? 0u : 1u});
    if (!texture)
    {
        return texture.error();
    }
    if (auto upload = texture.value().upload(0, image.rgba8); !upload)
    {
        return upload.error();
    }
    if (auto mips = texture.value().generateMipmaps(); !mips)
    {
        return mips.error();
    }
    return texture;
}

Result<rhi::Texture> createTexture(Device& device, const asset::TextureData& data, bool colour,
                                   std::optional<f32> alphaCutoff)
{
    auto generate = checkLevels(data);
    if (!generate)
    {
        return generate.error();
    }
    if (generate.value() && alphaCutoff)
    {
        asset::ImageData image;
        image.width = data.width();
        image.height = data.height();
        image.rgba8 = data.levels[0].data;
        return createCoverageTexture(device, image, colour, *alphaCutoff);
    }
    auto texture = device.createTexture({data.width(), data.height(), formatFor(data, colour),
                                         generate.value() ? 0u : static_cast<u32>(data.levels.size())});
    if (!texture)
    {
        return texture.error();
    }
    if (auto uploaded = uploadLevels(texture.value(), data, 0); !uploaded)
    {
        return uploaded.error();
    }
    if (generate.value())
    {
        if (auto mips = texture.value().generateMipmaps(); !mips)
        {
            return mips.error();
        }
    }
    return texture;
}

Result<rhi::Texture> createTextureArray(Device& device, std::span<const asset::TextureData* const> layers,
                                        TextureArrayUsage usage)
{
    if (layers.empty())
    {
        return Error{"texture array: no layers"};
    }
    const bool colour = usage == TextureArrayUsage::Colour;
    const asset::TextureData* first = layers[0];
    if (first == nullptr)
    {
        return Error{"texture array: layer 0 is missing"};
    }
    auto generate = checkLevels(*first);
    if (!generate)
    {
        return Error{"texture array: layer 0: " + generate.error().message};
    }
    const rhi::Format format = formatFor(*first, colour, !colour);
    for (usize i = 1; i < layers.size(); ++i)
    {
        const asset::TextureData* layer = layers[i];
        if (layer == nullptr)
        {
            return Error{"texture array: layer " + std::to_string(i) + " is missing"};
        }
        if (layer->width() != first->width() || layer->height() != first->height())
        {
            return Error{"texture array: layer " + std::to_string(i) + " is " +
                         std::to_string(layer->width()) + "x" + std::to_string(layer->height()) +
                         ", layer 0 is " + std::to_string(first->width()) + "x" +
                         std::to_string(first->height()) + " (all layers need the same size)"};
        }
        if (formatFor(*layer, colour, !colour) != format || layer->levels.size() != first->levels.size())
        {
            return Error{"texture array: layer " + std::to_string(i) +
                         " differs from layer 0 in format or mip levels (all layers need the same)"};
        }
    }
    auto texture = device.createTexture({first->width(), first->height(), format,
                                         generate.value() ? 0u : static_cast<u32>(first->levels.size()),
                                         static_cast<u32>(layers.size()), true});
    if (!texture)
    {
        return texture.error();
    }
    for (usize i = 0; i < layers.size(); ++i)
    {
        if (auto uploaded = uploadLevels(texture.value(), *layers[i], static_cast<u32>(i)); !uploaded)
        {
            return uploaded.error();
        }
    }
    if (generate.value())
    {
        if (auto mips = texture.value().generateMipmaps(); !mips)
        {
            return mips.error();
        }
    }
    return texture;
}

Result<rhi::Texture> createSolidTexture(Device& device, u8 r, u8 g, u8 b, u8 a, bool srgb)
{
    return createTexture(device, asset::ImageData{1, 1, {r, g, b, a}}, TextureUpload{srgb, false});
}
} // namespace g7::render

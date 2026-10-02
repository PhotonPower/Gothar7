#include <g7/render/Device.hpp>
#include <g7/render/TextureUpload.hpp>

#include <string>

namespace g7::render
{
Result<rhi::Texture> createTexture(Device& device, const asset::ImageData& image, TextureUpload options)
{
    if (image.width == 0 || image.height == 0 ||
        image.rgba8.size() != static_cast<usize>(image.width) * image.height * 4)
    {
        return Error{"invalid image data"};
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

Result<rhi::Texture> createTexture(Device& device, const asset::TextureData& data, bool colour)
{
    if (data.levels.empty() || data.width() == 0 || data.height() == 0)
    {
        return Error{"invalid texture data: no levels"};
    }
    rhi::Format format = rhi::Format::RGBA8;
    switch (data.format)
    {
    case asset::TextureFormat::RGBA8:
        format = colour ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8;
        break;
    case asset::TextureFormat::BC7:
        format = data.srgb ? rhi::Format::BC7_SRGB : rhi::Format::BC7;
        break;
    case asset::TextureFormat::BC5:
        format = rhi::Format::BC5;
        break;
    }
    // A single RGBA8 level gets a generated mip chain; everything else comes with its levels.
    const bool generate = data.format == asset::TextureFormat::RGBA8 && data.levels.size() == 1;
    const u32 fullChain = rhi::mipLevelCount(data.width(), data.height());
    if (data.levels.size() > fullChain)
    {
        return Error{"invalid texture data: " + std::to_string(data.levels.size()) + " levels for " +
                     std::to_string(data.width()) + "x" + std::to_string(data.height())};
    }
    auto texture = device.createTexture(
        {data.width(), data.height(), format, generate ? 0u : static_cast<u32>(data.levels.size())});
    if (!texture)
    {
        return texture.error();
    }
    for (usize level = 0; level < data.levels.size(); ++level)
    {
        const asset::TextureLevel& l = data.levels[level];
        if (l.width != rhi::mipSize(data.width(), static_cast<u32>(level)) ||
            l.height != rhi::mipSize(data.height(), static_cast<u32>(level)))
        {
            return Error{"invalid texture data: level " + std::to_string(level) + " has the wrong size"};
        }
        if (auto upload = texture.value().upload(static_cast<u32>(level), l.data); !upload)
        {
            return upload.error();
        }
    }
    if (generate)
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

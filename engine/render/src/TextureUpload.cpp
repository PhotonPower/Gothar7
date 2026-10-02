#include <g7/render/Device.hpp>
#include <g7/render/TextureUpload.hpp>

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

Result<rhi::Texture> createSolidTexture(Device& device, u8 r, u8 g, u8 b, u8 a, bool srgb)
{
    return createTexture(device, asset::ImageData{1, 1, {r, g, b, a}}, TextureUpload{srgb, false});
}
} // namespace g7::render

#include <g7/asset/ImageData.hpp>

#include <stb_image.h>

#include <cstring>
#include <limits>
#include <string>

namespace g7::asset
{
Result<ImageData> decodeImage(std::span<const u8> bytes, std::string_view debugName)
{
    if (bytes.empty() || bytes.size() > static_cast<usize>(std::numeric_limits<int>::max()))
    {
        return Error{std::string(debugName) + ": no image data"};
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                                            &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        return Error{std::string(debugName) + ": cannot decode image (" + stbi_failure_reason() + ")"};
    }
    ImageData image;
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    image.rgba8.resize(static_cast<usize>(width) * static_cast<usize>(height) * 4);
    std::memcpy(image.rgba8.data(), pixels, image.rgba8.size());
    stbi_image_free(pixels);
    return image;
}

Result<ImageData> loadImage(const fs::Path& path)
{
    auto bytes = fs::readFile(path);
    if (!bytes)
    {
        return bytes.error();
    }
    return decodeImage(bytes.value(), fs::toUtf8(path));
}
} // namespace g7::asset

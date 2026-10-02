// PNG encoding with stb_image_write (ADR 0014). STB_IMAGE_WRITE_STATIC keeps its symbols local to
// this file, so tests may compile their own copy for generating test images.

#include <g7/asset/ImageData.hpp>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO // written through fs:: (UTF-8 paths, atomic)

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <stb_image_write.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <limits>
#include <string>

namespace g7::asset
{
namespace
{
void append(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<u8>*>(context);
    const auto* bytes = static_cast<const u8*>(data);
    out->insert(out->end(), bytes, bytes + size);
}
} // namespace

Result<std::vector<u8>> encodePng(const ImageData& image)
{
    constexpr u32 kMaxSide = static_cast<u32>(std::numeric_limits<int>::max() / 4);
    if (image.width == 0 || image.height == 0 || image.width > kMaxSide ||
        image.rgba8.size() != static_cast<usize>(image.width) * image.height * 4)
    {
        return Error{"encodePng: image size and pixel data do not match"};
    }
    std::vector<u8> png;
    const int stride = static_cast<int>(image.width * 4);
    if (stbi_write_png_to_func(append, &png, static_cast<int>(image.width), static_cast<int>(image.height), 4,
                               image.rgba8.data(), stride) == 0)
    {
        return Error{"encodePng: encoding failed"};
    }
    return png;
}

Result<void> savePng(const fs::Path& path, const ImageData& image)
{
    auto png = encodePng(image);
    if (!png)
    {
        return png.error();
    }
    return fs::writeFileAtomic(path, png.value());
}
} // namespace g7::asset

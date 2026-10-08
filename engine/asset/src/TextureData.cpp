#include <g7/asset/TextureData.hpp>

#include <string>

#if G7_HAS_KTX
#include <ktx.h>
#endif

namespace g7::asset
{
TextureData textureFromImage(ImageData image, bool srgb)
{
    TextureData texture;
    texture.format = TextureFormat::RGBA8;
    texture.srgb = srgb;
    texture.levels.push_back({image.width, image.height, std::move(image.rgba8)});
    return texture;
}

#if G7_HAS_KTX
namespace
{
// VkFormat values used by g7-cook (libktx does not ship vulkan.h).
constexpr ktx_uint32_t kVkR8G8B8A8Unorm = 37;
constexpr ktx_uint32_t kVkR8G8B8A8Srgb = 43;

Error ktxError(std::string_view debugName, std::string_view what, KTX_error_code code)
{
    return Error{std::string(debugName) + ": " + std::string(what) + " (" + ktxErrorString(code) + ")"};
}

/// Owns a ktxTexture2.
struct KtxTexture
{
    ktxTexture2* ptr = nullptr;
    ~KtxTexture()
    {
        if (ptr)
        {
            ktxTexture2_Destroy(ptr);
        }
    }
};
} // namespace

bool hasKtx2Support() noexcept
{
    return true;
}

Result<ImageData> decodeKtx2Rgba(std::span<const u8> bytes, std::string_view debugName)
{
    KtxTexture tex;
    const KTX_error_code created = ktxTexture2_CreateFromMemory(
        bytes.data(), bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex.ptr);
    if (created != KTX_SUCCESS)
    {
        return ktxError(debugName, "not a readable KTX2 file", created);
    }
    ktxTexture2* t = tex.ptr;
    if (ktxTexture2_NeedsTranscoding(t))
    {
        if (const KTX_error_code transcoded = ktxTexture2_TranscodeBasis(t, KTX_TTF_RGBA32, 0);
            transcoded != KTX_SUCCESS)
        {
            return ktxError(debugName, "cannot transcode", transcoded);
        }
    }
    else if (t->vkFormat != kVkR8G8B8A8Unorm && t->vkFormat != kVkR8G8B8A8Srgb)
    {
        return Error{std::string(debugName) + ": unsupported KTX2 format (vkFormat " +
                     std::to_string(t->vkFormat) + ")"};
    }
    ktx_size_t offset = 0;
    if (ktxTexture_GetImageOffset(ktxTexture(t), 0, 0, 0, &offset) != KTX_SUCCESS)
    {
        return Error{std::string(debugName) + ": broken mip level 0"};
    }
    const ktx_size_t size = ktxTexture_GetImageSize(ktxTexture(t), 0);
    const u8* data = ktxTexture_GetData(ktxTexture(t)) + offset;
    ImageData image;
    image.width = t->baseWidth;
    image.height = t->baseHeight;
    if (size < static_cast<ktx_size_t>(image.width) * image.height * 4)
    {
        return Error{std::string(debugName) + ": level 0 too small"};
    }
    image.rgba8.assign(data, data + static_cast<usize>(image.width) * image.height * 4);
    return image;
}

Result<TextureData> decodeKtx2(std::span<const u8> bytes, std::string_view debugName)
{
    KtxTexture tex;
    const KTX_error_code created = ktxTexture2_CreateFromMemory(
        bytes.data(), bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex.ptr);
    if (created != KTX_SUCCESS)
    {
        return ktxError(debugName, "not a readable KTX2 file", created);
    }
    ktxTexture2* t = tex.ptr;
    if (t->numDimensions != 2 || t->numFaces != 1 || t->numLayers != 1 || t->baseWidth == 0 ||
        t->baseHeight == 0)
    {
        return Error{std::string(debugName) + ": only plain 2D KTX2 textures are supported"};
    }

    TextureData texture;
    texture.srgb = ktxTexture2_GetOETF_e(t) == KHR_DF_TRANSFER_SRGB;
    const ktx_uint32_t components = ktxTexture2_GetNumComponents(t);
    if (ktxTexture2_NeedsTranscoding(t))
    {
        const bool twoChannel = components == 2;
        if (twoChannel && texture.srgb)
        {
            return Error{std::string(debugName) + ": two-channel data must be linear (BC5 has no sRGB)"};
        }
        const KTX_error_code transcoded =
            ktxTexture2_TranscodeBasis(t, twoChannel ? KTX_TTF_BC5_RG : KTX_TTF_BC7_RGBA, 0);
        if (transcoded != KTX_SUCCESS)
        {
            return ktxError(debugName, "cannot transcode", transcoded);
        }
        texture.format = twoChannel ? TextureFormat::BC5 : TextureFormat::BC7;
    }
    else if (t->vkFormat == kVkR8G8B8A8Unorm || t->vkFormat == kVkR8G8B8A8Srgb)
    {
        texture.format = TextureFormat::RGBA8;
    }
    else
    {
        // g7-cook writes UASTC; uncompressed files are accepted as RGBA8 only.
        return Error{std::string(debugName) + ": unsupported KTX2 format (vkFormat " +
                     std::to_string(t->vkFormat) + ")"};
    }

    ktxTexture* base = ktxTexture(t);
    for (ktx_uint32_t level = 0; level < t->numLevels; ++level)
    {
        ktx_size_t offset = 0;
        if (ktxTexture_GetImageOffset(base, level, 0, 0, &offset) != KTX_SUCCESS)
        {
            return Error{std::string(debugName) + ": broken mip level " + std::to_string(level)};
        }
        const ktx_size_t size = ktxTexture_GetImageSize(base, level);
        const u8* data = ktxTexture_GetData(base) + offset;
        TextureLevel out;
        out.width = std::max(1u, t->baseWidth >> level);
        out.height = std::max(1u, t->baseHeight >> level);
        out.data.assign(data, data + size);
        texture.levels.push_back(std::move(out));
    }
    return texture;
}
#else
bool hasKtx2Support() noexcept
{
    return false;
}

Result<TextureData> decodeKtx2(std::span<const u8>, std::string_view debugName)
{
    return Error{std::string(debugName) + ": this build has no KTX2 support (libktx missing)"};
}

Result<ImageData> decodeKtx2Rgba(std::span<const u8>, std::string_view debugName)
{
    return Error{std::string(debugName) + ": this build has no KTX2 support (libktx missing)"};
}
#endif
} // namespace g7::asset

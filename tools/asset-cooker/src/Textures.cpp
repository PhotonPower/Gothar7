#include "Textures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <thread>

#if G7_HAS_KTX
#include <ktx.h>
#endif

namespace g7::cook
{
namespace
{
f32 srgbToLinear(f32 c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

f32 linearToSrgb(f32 c)
{
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

const std::array<f32, 256>& srgbTable()
{
    static const std::array<f32, 256> table = []
    {
        std::array<f32, 256> t{};
        for (usize i = 0; i < t.size(); ++i)
        {
            t[i] = srgbToLinear(static_cast<f32>(i) / 255.0f);
        }
        return t;
    }();
    return table;
}

u8 toByte(f32 v)
{
    return static_cast<u8>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
}

asset::ImageData halve(const asset::ImageData& src, TextureUsage usage)
{
    asset::ImageData dst;
    dst.width = std::max(1u, src.width / 2);
    dst.height = std::max(1u, src.height / 2);
    dst.rgba8.resize(usize(dst.width) * dst.height * 4);
    const auto& lin = srgbTable();
    for (u32 y = 0; y < dst.height; ++y)
    {
        for (u32 x = 0; x < dst.width; ++x)
        {
            f32 sum[4] = {0, 0, 0, 0};
            for (u32 dy = 0; dy < 2; ++dy)
            {
                for (u32 dx = 0; dx < 2; ++dx)
                {
                    const u32 sx = std::min(2 * x + dx, src.width - 1);
                    const u32 sy = std::min(2 * y + dy, src.height - 1);
                    const u8* p = &src.rgba8[(usize(sy) * src.width + sx) * 4];
                    for (int c = 0; c < 3; ++c)
                    {
                        sum[c] += usage == TextureUsage::Color ? lin[p[c]] : p[c] / 255.0f * 2.0f - 1.0f;
                    }
                    sum[3] += p[3] / 255.0f;
                }
            }
            u8* out = &dst.rgba8[(usize(y) * dst.width + x) * 4];
            if (usage == TextureUsage::Color)
            {
                for (int c = 0; c < 3; ++c)
                {
                    out[c] = toByte(linearToSrgb(sum[c] / 4.0f));
                }
            }
            else
            {
                f32 len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                const f32 n[3] = {len > 1e-6f ? sum[0] / len : 0.0f, len > 1e-6f ? sum[1] / len : 0.0f,
                                  len > 1e-6f ? sum[2] / len : 1.0f};
                for (int c = 0; c < 3; ++c)
                {
                    out[c] = toByte(n[c] * 0.5f + 0.5f);
                }
            }
            out[3] = toByte(sum[3] / 4.0f);
        }
    }
    return dst;
}
} // namespace

std::vector<asset::ImageData> buildMipChain(const asset::ImageData& image, TextureUsage usage)
{
    std::vector<asset::ImageData> chain{image};
    while (chain.back().width > 1 || chain.back().height > 1)
    {
        chain.push_back(halve(chain.back(), usage));
    }
    return chain;
}

#if G7_HAS_KTX
namespace
{
constexpr ktx_uint32_t kVkR8G8Unorm = 16;
constexpr ktx_uint32_t kVkR8G8B8A8Srgb = 43;
constexpr ktx_uint32_t kZstdLevel = 18;

Error ktxError(std::string_view what, KTX_error_code code)
{
    return Error{std::string(what) + " (" + ktxErrorString(code) + ")"};
}

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

bool hasKtx2Encoder() noexcept
{
    return true;
}

Result<std::vector<u8>> encodeKtx2(const asset::ImageData& image, TextureUsage usage, u32 uastcLevel)
{
    if (image.width == 0 || image.height == 0)
    {
        return Error{"empty image"};
    }
    const std::vector<asset::ImageData> chain = buildMipChain(image, usage);
    const bool normal = usage == TextureUsage::Normal;

    ktxTextureCreateInfo info{};
    info.vkFormat = normal ? kVkR8G8Unorm : kVkR8G8B8A8Srgb;
    info.baseWidth = image.width;
    info.baseHeight = image.height;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = static_cast<ktx_uint32_t>(chain.size());
    info.numLayers = 1;
    info.numFaces = 1;
    info.isArray = KTX_FALSE;
    info.generateMipmaps = KTX_FALSE;

    KtxTexture tex;
    if (const auto rc = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex.ptr);
        rc != KTX_SUCCESS)
    {
        return ktxError("cannot create KTX2 texture", rc);
    }
    for (usize level = 0; level < chain.size(); ++level)
    {
        const asset::ImageData& mip = chain[level];
        std::vector<u8> pixels;
        if (normal)
        {
            pixels.reserve(usize(mip.width) * mip.height * 2);
            for (usize i = 0; i < mip.rgba8.size(); i += 4)
            {
                pixels.push_back(mip.rgba8[i]);     // X
                pixels.push_back(mip.rgba8[i + 1]); // Y; Z is rebuilt in the shader
            }
        }
        else
        {
            pixels = mip.rgba8;
        }
        const auto rc = ktxTexture_SetImageFromMemory(ktxTexture(tex.ptr), static_cast<ktx_uint32_t>(level),
                                                      0, 0, pixels.data(), pixels.size());
        if (rc != KTX_SUCCESS)
        {
            return ktxError("cannot set mip level " + std::to_string(level), rc);
        }
    }

    ktxBasisParams params{};
    params.structSize = sizeof(params);
    params.uastc = KTX_TRUE;
    params.uastcFlags = std::min<u32>(uastcLevel, KTX_PACK_UASTC_MAX_LEVEL);
    params.threadCount = std::max(1u, std::thread::hardware_concurrency());
    params.normalMap = normal ? KTX_TRUE : KTX_FALSE;
    if (const auto rc = ktxTexture2_CompressBasisEx(tex.ptr, &params); rc != KTX_SUCCESS)
    {
        return ktxError("UASTC encoding failed", rc);
    }
    if (const auto rc = ktxTexture2_DeflateZstd(tex.ptr, kZstdLevel); rc != KTX_SUCCESS)
    {
        return ktxError("zstd supercompression failed", rc);
    }
    ktx_uint8_t* bytes = nullptr;
    ktx_size_t size = 0;
    if (const auto rc = ktxTexture_WriteToMemory(ktxTexture(tex.ptr), &bytes, &size); rc != KTX_SUCCESS)
    {
        return ktxError("cannot write KTX2", rc);
    }
    std::vector<u8> out(bytes, bytes + size);
    std::free(bytes);
    return out;
}
#else
bool hasKtx2Encoder() noexcept
{
    return false;
}

Result<std::vector<u8>> encodeKtx2(const asset::ImageData&, TextureUsage, u32)
{
    return Error{"this build has no KTX2 support (libktx missing)"};
}
#endif
} // namespace g7::cook

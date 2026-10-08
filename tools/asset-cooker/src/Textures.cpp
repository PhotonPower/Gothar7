#include "Textures.hpp"

#include <g7/asset/ImageMips.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>

#if G7_HAS_KTX
#include <ktx.h>
#endif

namespace g7::cook
{
std::vector<asset::ImageData> buildMipChain(const asset::ImageData& image, TextureUsage usage)
{
    return asset::buildMipChain(image, usage == TextureUsage::Color  ? asset::MipFilter::Srgb
                                       : usage == TextureUsage::Data ? asset::MipFilter::Linear
                                                                     : asset::MipFilter::Normal);
}

#if G7_HAS_KTX
namespace
{
constexpr ktx_uint32_t kVkR8G8Unorm = 16;
constexpr ktx_uint32_t kVkR8G8B8A8Unorm = 37;
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

Result<std::vector<u8>> encodeKtx2(const asset::ImageData& image, TextureUsage usage, u32 uastcLevel,
                                   std::optional<f32> alphaCutoff)
{
    if (image.width == 0 || image.height == 0)
    {
        return Error{"empty image"};
    }
    std::vector<asset::ImageData> chain = buildMipChain(image, usage);
    if (alphaCutoff && usage == TextureUsage::Color)
    {
        asset::preserveAlphaCoverage(chain, *alphaCutoff); // alpha-tested: as dense in the distance
    }
    const bool normal = usage == TextureUsage::Normal;

    ktxTextureCreateInfo info{};
    info.vkFormat = normal ? kVkR8G8Unorm : usage == TextureUsage::Data ? kVkR8G8B8A8Unorm : kVkR8G8B8A8Srgb;
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
    // One thread: basisu's job_pool (libktx 4.4.2) can hang for ever when it is destroyed - its destructor
    // sets the kill flag without holding the mutex, so a worker about to wait misses the wake-up and
    // join() blocks (lost wake-up; reproduced about once per 1000-2000 encodes, docs/modules/asset.md).
    // With one thread the pool starts no workers; the cooker encodes several textures in parallel instead.
    params.threadCount = 1;
    params.normalMap = normal ? KTX_TRUE : KTX_FALSE;
    // The first encode initialises basisu, and libktx guards that with a plain bool: serialise encodes
    // until one has finished, then they run in parallel.
    static std::mutex s_firstEncode;
    static std::atomic<bool> s_initialised{false};
    std::unique_lock first(s_firstEncode, std::defer_lock);
    if (!s_initialised.load(std::memory_order_acquire))
    {
        first.lock();
    }
    if (const auto rc = ktxTexture2_CompressBasisEx(tex.ptr, &params); rc != KTX_SUCCESS)
    {
        return ktxError("UASTC encoding failed", rc);
    }
    if (first.owns_lock())
    {
        s_initialised.store(true, std::memory_order_release);
        first.unlock();
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

Result<std::vector<u8>> encodeKtx2(const asset::ImageData&, TextureUsage, u32, std::optional<f32>)
{
    return Error{"this build has no KTX2 support (libktx missing)"};
}
#endif
} // namespace g7::cook

// CPU mip chains and alpha coverage (see ImageMips.hpp).

#include <g7/asset/ImageMips.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace g7::asset
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
} // namespace

ImageData halveImage(const ImageData& src, MipFilter filter)
{
    ImageData dst;
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
                        sum[c] += filter == MipFilter::Srgb     ? lin[p[c]]
                                  : filter == MipFilter::Linear ? p[c] / 255.0f
                                                                : p[c] / 255.0f * 2.0f - 1.0f;
                    }
                    sum[3] += p[3] / 255.0f;
                }
            }
            u8* out = &dst.rgba8[(usize(y) * dst.width + x) * 4];
            if (filter == MipFilter::Srgb)
            {
                for (int c = 0; c < 3; ++c)
                {
                    out[c] = toByte(linearToSrgb(sum[c] / 4.0f));
                }
            }
            else if (filter == MipFilter::Linear)
            {
                for (int c = 0; c < 3; ++c)
                {
                    out[c] = toByte(sum[c] / 4.0f);
                }
            }
            else
            {
                const f32 len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
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

std::vector<ImageData> buildMipChain(const ImageData& image, MipFilter filter)
{
    std::vector<ImageData> chain{image};
    while (chain.back().width > 1 || chain.back().height > 1)
    {
        chain.push_back(halveImage(chain.back(), filter));
    }
    return chain;
}

f32 alphaCoverage(const ImageData& image, f32 cutoff, f32 scale) noexcept
{
    const usize texels = usize(image.width) * image.height;
    if (texels == 0)
    {
        return 0.0f;
    }
    usize covered = 0;
    for (usize i = 0; i < texels; ++i)
    {
        if (static_cast<f32>(image.rgba8[i * 4 + 3]) / 255.0f * scale >= cutoff)
        {
            ++covered;
        }
    }
    return static_cast<f32>(covered) / static_cast<f32>(texels);
}

void preserveAlphaCoverage(std::span<ImageData> chain, f32 cutoff)
{
    if (chain.size() < 2 || cutoff <= 0.0f || cutoff >= 1.0f)
    {
        return;
    }
    // Alpha comes in bytes: the alpha test passes from this byte on (0.5 -> 128).
    const auto cutoffByte = static_cast<u32>(std::clamp(std::ceil(cutoff * 255.0f - 1e-4f), 1.0f, 255.0f));
    const f32 target = alphaCoverage(chain[0], cutoff);
    for (usize level = 1; level < chain.size(); ++level)
    {
        ImageData& image = chain[level];
        const usize texels = usize(image.width) * image.height;
        if (texels == 0)
        {
            continue;
        }
        // How many texels reach each byte: the threshold whose coverage comes nearest level 0's ...
        std::array<usize, 257> atLeast{};
        for (usize i = 3; i < image.rgba8.size(); i += 4)
        {
            ++atLeast[image.rgba8[i]];
        }
        for (i32 b = 254; b >= 0; --b)
        {
            atLeast[static_cast<usize>(b)] += atLeast[static_cast<usize>(b) + 1];
        }
        u32 threshold = cutoffByte;
        f32 best = 2.0f;
        for (u32 t = 1; t <= 255; ++t)
        {
            const f32 error = std::abs(static_cast<f32>(atLeast[t]) / static_cast<f32>(texels) - target);
            if (error < best || (error == best && t == cutoffByte))
            {
                best = error;
                threshold = t;
            }
        }
        // ... is moved onto the cutoff: bytes from it on pass the test, those below do not.
        const f32 scale = static_cast<f32>(cutoffByte) / static_cast<f32>(threshold);
        for (usize i = 3; i < image.rgba8.size(); i += 4)
        {
            image.rgba8[i] =
                static_cast<u8>(std::min(255L, std::lround(static_cast<f32>(image.rgba8[i]) * scale)));
        }
    }
}
} // namespace g7::asset

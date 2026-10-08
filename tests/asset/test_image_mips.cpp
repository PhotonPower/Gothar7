// CPU mip chains and alpha coverage (asset/ImageMips.hpp): a stubble-like texture - scattered opaque dots on
// transparent - thins out in plain mips (an alpha test at 0.5 drops almost all of it a few levels down) but
// keeps its coverage when preserveAlphaCoverage scales the levels.

#include <g7/asset/ImageMips.hpp>

#include <doctest/doctest.h>

#include <random>

using namespace g7;
using namespace g7::asset;

namespace
{
/// `size` x `size`, a share `density` of the texels opaque (alpha 255) at random, the rest transparent.
ImageData stubble(u32 size, f32 density)
{
    ImageData image{size, size, {}};
    std::mt19937 rng(5);
    std::uniform_real_distribution<f32> roll(0.0f, 1.0f);
    for (u32 i = 0; i < size * size; ++i)
    {
        const u8 a = roll(rng) < density ? 255 : 0;
        image.rgba8.insert(image.rgba8.end(), {60, 45, 30, a});
    }
    return image;
}
} // namespace

TEST_CASE("image mips: the chain, and the coverage measured at a cutoff")
{
    const ImageData image = stubble(64, 0.3f);
    const auto chain = buildMipChain(image, MipFilter::Srgb);
    REQUIRE(chain.size() == 7);
    CHECK(chain[6].width == 1);
    CHECK(alphaCoverage(image, 0.5f) == doctest::Approx(0.3f).epsilon(0.1));
    CHECK(alphaCoverage(image, 0.5f, 0.0f) == 0.0f);
    // Plain mips: the dots average to about 0.3 alpha - an alpha test at 0.5 loses nearly all of them.
    CHECK(alphaCoverage(chain[3], 0.5f) < 0.1f);
    // A cutoff of 0 or 1 is no alpha test: nothing changes.
    auto same = chain;
    preserveAlphaCoverage(same, 0.0f);
    CHECK(same[3].rgba8 == chain[3].rgba8);
}

TEST_CASE("image mips: preserveAlphaCoverage keeps every level as dense as level 0")
{
    for (const f32 cutoff : {0.5f, 0.33f})
    {
        CAPTURE(cutoff);
        const ImageData image = stubble(128, 0.25f);
        auto chain = buildMipChain(image, MipFilter::Srgb);
        preserveAlphaCoverage(chain, cutoff);
        const f32 target = alphaCoverage(chain[0], cutoff);
        for (usize level = 1; level + 2 < chain.size(); ++level) // down to 4 x 4 (16 texels: coarse steps)
        {
            CAPTURE(level);
            CHECK(std::abs(alphaCoverage(chain[level], cutoff) - target) < 0.08f);
        }
        // Only alpha changes; level 0 stays the image.
        CHECK(chain[0].rgba8 == image.rgba8);
        const auto plain = buildMipChain(image, MipFilter::Srgb);
        for (usize i = 0; i < plain[2].rgba8.size(); i += 4)
        {
            CHECK(chain[2].rgba8[i] == plain[2].rgba8[i]);
        }
    }
    // All transparent: nothing to scale.
    ImageData empty{8, 8, std::vector<u8>(8 * 8 * 4, 0)};
    auto chain = buildMipChain(empty, MipFilter::Linear);
    preserveAlphaCoverage(chain, 0.5f);
    CHECK(alphaCoverage(chain[1], 0.5f) == 0.0f);
}

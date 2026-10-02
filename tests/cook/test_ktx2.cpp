#include "../asset/TempDir.hpp"
#include "Cooker.hpp"
#include "Textures.hpp"

#include <g7/asset/AssetManager.hpp>
#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshFile.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/asset/Vfs.hpp>

#include <doctest/doctest.h>

#if G7_HAS_KTX
#include <ktx.h>
#endif

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <string_view>
#include <vector>

using namespace g7;

namespace
{
asset::ImageData solid(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a = 255)
{
    asset::ImageData img{w, h, {}};
    for (u32 i = 0; i < w * h; ++i)
    {
        img.rgba8.insert(img.rgba8.end(), {r, g, b, a});
    }
    return img;
}

std::vector<u8> png(const asset::ImageData& img)
{
    auto encoded = asset::encodePng(img);
    REQUIRE(encoded);
    return std::move(encoded).value();
}

void write(const fs::Path& path, std::span<const u8> data)
{
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(fs::writeFile(path, data));
}

void writeText(const fs::Path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(fs::writeText(path, text));
}

/// Triangle .gltf (external .bin) whose material uses a colour and a normal texture.
std::string gltfWithTextures(std::string_view colorUri, std::string_view normalUri)
{
    return std::string(
               R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],)"
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
               R"("materials":[{"name":"Putz","pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"normalTexture":{"index":1}}],)"
               R"("textures":[{"source":0},{"source":1}],"images":[{"uri":")") +
           std::string(colorUri) + R"("},{"uri":")" + std::string(normalUri) + R"("}],)" +
           R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"buffers":[{"byteLength":36,"uri":"wall.bin"}]})";
}

std::vector<u8> triangleBin()
{
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::vector<u8> bin(sizeof(positions));
    std::memcpy(bin.data(), positions, sizeof(positions));
    return bin;
}

struct KtxTree
{
    test::TempDir dir;
    fs::Path source = dir.path() / "source";
    fs::Path out = dir.path() / "cooked";

    KtxTree()
    {
        write(source / "textures" / "plaster.png", png(solid(16, 8, 200, 120, 40)));
        // Flat normal (0, 0, 1) -> RGB (128, 128, 255).
        write(source / "textures" / "plaster_n.png", png(solid(16, 8, 128, 128, 255)));
        write(source / "textures" / "unused.png", png(solid(4, 4, 10, 20, 30)));
        write(source / "models" / "wall.bin", triangleBin());
        writeText(source / "models" / "wall.gltf",
                  gltfWithTextures("../textures/plaster.png", "../textures/plaster_n.png"));
    }
};

/// Decodes the first texel of a BC4 block (8 bytes: two endpoints, 16 x 3-bit indices), as used
/// twice per BC5 block (R, then G).
u8 bc4FirstTexel(const u8* block)
{
    const u32 r0 = block[0];
    const u32 r1 = block[1];
    const u32 index = block[2] & 0x7; // texel 0 = lowest three index bits
    if (index == 0)
    {
        return static_cast<u8>(r0);
    }
    if (index == 1)
    {
        return static_cast<u8>(r1);
    }
    if (r0 > r1)
    {
        return static_cast<u8>(((8 - index) * r0 + (index - 1) * r1) / 7);
    }
    if (index == 6)
    {
        return 0;
    }
    if (index == 7)
    {
        return 255;
    }
    return static_cast<u8>(((6 - index) * r0 + (index - 1) * r1) / 5);
}

#if G7_HAS_KTX
/// Decodes a .ktx2 to RGBA8 pixels of level 0 through libktx (independent of the BC formats).
std::vector<u8> toRgba(const std::vector<u8>& ktx2)
{
    ktxTexture2* tex = nullptr;
    REQUIRE(ktxTexture2_CreateFromMemory(ktx2.data(), ktx2.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                         &tex) == KTX_SUCCESS);
    REQUIRE(ktxTexture2_TranscodeBasis(tex, KTX_TTF_RGBA32, 0) == KTX_SUCCESS);
    ktx_size_t offset = 0;
    REQUIRE(ktxTexture_GetImageOffset(ktxTexture(tex), 0, 0, 0, &offset) == KTX_SUCCESS);
    const ktx_size_t size = ktxTexture_GetImageSize(ktxTexture(tex), 0);
    const u8* data = ktxTexture_GetData(ktxTexture(tex)) + offset;
    std::vector<u8> pixels(data, data + size);
    ktxTexture2_Destroy(tex);
    return pixels;
}
#endif
} // namespace

TEST_CASE("mip chain: sizes down to 1x1, colour averaged in linear light, normals renormalised")
{
    asset::ImageData img{5, 2, {}};
    for (u32 i = 0; i < 10; ++i)
    {
        const u8 v = (i % 2 == 0) ? 0 : 255; // black/white columns
        img.rgba8.insert(img.rgba8.end(), {v, v, v, 255});
    }
    const auto chain = cook::buildMipChain(img, cook::TextureUsage::Color);
    REQUIRE(chain.size() == 3);
    CHECK((chain[1].width == 2 && chain[1].height == 1));
    CHECK((chain[2].width == 1 && chain[2].height == 1));
    // Black + white averaged in linear light is 0.5 linear = sRGB 188, not 128.
    CHECK(std::abs(int(chain[1].rgba8[0]) - 188) <= 1);

    asset::ImageData normals{2, 1, {}};
    // (1,0,0) and (0,0,1): the average (0.5,0,0.5) renormalises to (0.707,0,0.707).
    normals.rgba8 = {255, 128, 128, 255, 128, 128, 255, 255};
    const auto n = cook::buildMipChain(normals, cook::TextureUsage::Normal);
    REQUIRE(n.size() == 2);
    CHECK(std::abs(int(n[1].rgba8[0]) - 218) <= 1); // 0.707 * 0.5 + 0.5 -> 218
    CHECK(std::abs(int(n[1].rgba8[2]) - 218) <= 1);
}

#if G7_HAS_KTX
TEST_CASE("KTX2 colour texture: BC7 sRGB with full mip chain and the right colour")
{
    REQUIRE(asset::hasKtx2Support());
    auto ktx = cook::encodeKtx2(solid(64, 32, 200, 120, 40), cook::TextureUsage::Color);
    REQUIRE_MESSAGE(ktx, (ktx ? "" : ktx.error().message));

    auto tex = asset::decodeKtx2(ktx.value(), "plaster.ktx2");
    REQUIRE_MESSAGE(tex, (tex ? "" : tex.error().message));
    CHECK(tex.value().format == asset::TextureFormat::BC7);
    CHECK(tex.value().srgb);
    REQUIRE(tex.value().levels.size() == 7); // 64x32 .. 1x1
    CHECK(tex.value().width() == 64);
    CHECK(tex.value().levels[1].width == 32);
    CHECK(tex.value().levels[6].height == 1);
    CHECK(tex.value().levels[0].data.size() == 16u * 8u * 16u); // 16 x 8 blocks of 16 bytes

    const auto rgba = toRgba(ktx.value());
    CHECK(std::abs(int(rgba[0]) - 200) <= 3);
    CHECK(std::abs(int(rgba[1]) - 120) <= 3);
    CHECK(std::abs(int(rgba[2]) - 40) <= 3);
    CHECK(rgba[3] == 255);
}

TEST_CASE("KTX2 normal map: two channels, linear, BC5 with X in R and Y in G")
{
    // X = 200, Y = 60 (Z is dropped): BC5 must carry X in the first (R) and Y in the second (G) half.
    auto ktx = cook::encodeKtx2(solid(16, 12, 200, 60, 230), cook::TextureUsage::Normal);
    REQUIRE(ktx);
    auto tex = asset::decodeKtx2(ktx.value());
    REQUIRE(tex);
    CHECK(tex.value().format == asset::TextureFormat::BC5);
    CHECK_FALSE(tex.value().srgb);
    REQUIRE(tex.value().levels.size() == 5); // 16x12, 8x6, 4x3, 2x1, 1x1

    // Every level holds whole 4 x 4 blocks of 16 bytes: ceil(w/4) * ceil(h/4) * 16.
    for (const auto& level : tex.value().levels)
    {
        CHECK(level.data.size() == ((level.width + 3) / 4) * ((level.height + 3) / 4) * 16u);
    }
    const u8* block = tex.value().levels[0].data.data();
    CHECK(std::abs(int(bc4FirstTexel(block)) - 200) <= 2);    // R = X
    CHECK(std::abs(int(bc4FirstTexel(block + 8)) - 60) <= 2); // G = Y
}

TEST_CASE("cook --textures ktx2: normal maps detected, mesh refers to .ktx2, deterministic")
{
    KtxTree t;
    const cook::CookOptions options{.source = t.source,
                                    .out = t.out,
                                    .pack = "data.g7pak",
                                    .textures = cook::TextureMode::Ktx2,
                                    .uastcLevel = 0};
    auto report = cook::cook(options);
    REQUIRE(report);
    CHECK(report.value().errors.empty());
    CHECK(report.value().images == 3);

    asset::Vfs vfs;
    REQUIRE(vfs.mount(t.out / "data.g7pak", 0));
    CHECK(vfs.exists("textures/plaster.ktx2"));
    CHECK_FALSE(vfs.exists("textures/plaster.png"));

    asset::AssetManager assets(vfs);
    const auto mesh = assets.load<asset::MeshData>("models/wall.g7mesh");
    assets.waitAll();
    REQUIRE(mesh.isReady());
    REQUIRE(mesh->images.size() == 2);
    CHECK(mesh->images[0].uri == "textures/plaster.ktx2");
    CHECK(mesh->images[1].uri == "textures/plaster_n.ktx2");
    CHECK(mesh->images[0].mimeType == "image/ktx2");

    const auto color = assets.load<asset::TextureData>(mesh->images[0].uri);
    const auto normal = assets.load<asset::TextureData>(mesh->images[1].uri);
    const auto unused = assets.load<asset::TextureData>("textures/unused.ktx2");
    assets.waitAll();
    REQUIRE_MESSAGE(color.isReady(), color.error());
    CHECK(color->format == asset::TextureFormat::BC7);
    CHECK(color->srgb);
    REQUIRE(normal.isReady());
    CHECK(normal->format == asset::TextureFormat::BC5);
    REQUIRE(unused.isReady()); // not referenced by a mesh: colour
    CHECK(unused->format == asset::TextureFormat::BC7);

    const auto first = fs::readFile(t.out / "data.g7pak").value();
    REQUIRE(cook::cook(options));
    CHECK(fs::readFile(t.out / "data.g7pak").value() == first);
}

TEST_CASE("cook --textures ktx2: a texture used as colour and as normal map is an error")
{
    KtxTree t;
    writeText(t.source / "models" / "wrong.gltf",
              gltfWithTextures("../textures/plaster_n.png", "../textures/plaster_n.png"));
    write(t.source / "models" / "wrong.bin", triangleBin());
    std::string gltf = gltfWithTextures("../textures/plaster_n.png", "../textures/plaster_n.png");
    gltf.replace(gltf.find("wall.bin"), 8, "wrong.bin");
    writeText(t.source / "models" / "wrong.gltf", gltf);

    auto report =
        cook::cook({.source = t.source, .out = t.out, .textures = cook::TextureMode::Ktx2, .uastcLevel = 0});
    REQUIRE(report);
    REQUIRE(report.value().errors.size() == 1);
    CHECK(report.value().errors[0].find("textures/plaster_n.png") != std::string::npos);
    CHECK(report.value().errors[0].find("normal map") != std::string::npos);
}
#endif

TEST_CASE("textureFromImage and decodeKtx2 errors")
{
    const auto tex = asset::textureFromImage(solid(2, 2, 1, 2, 3), false);
    CHECK(tex.format == asset::TextureFormat::RGBA8);
    CHECK_FALSE(tex.srgb);
    REQUIRE(tex.levels.size() == 1);
    CHECK(tex.width() == 2);
    CHECK(tex.levels[0].data.size() == 16);

    const std::vector<u8> garbage = {'n', 'o', 't', 'k', 't', 'x'};
    auto bad = asset::decodeKtx2(garbage, "bad.ktx2");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.find("bad.ktx2") != std::string::npos);
}

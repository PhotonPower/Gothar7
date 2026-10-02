#include <g7/asset/ImageData.hpp>
#include <g7/asset/MeshData.hpp>

#include <doctest/doctest.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <ostream> // doctest needs it to print std::string_view operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
void appendBytes(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<u8>*>(context);
    const auto* bytes = static_cast<const u8*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::vector<u8> encodePng(int width, int height, int channels, const std::vector<u8>& pixels)
{
    std::vector<u8> png;
    REQUIRE(stbi_write_png_to_func(appendBytes, &png, width, height, channels, pixels.data(),
                                   width * channels) != 0);
    return png;
}

std::vector<u8> encodeJpeg(int width, int height, const std::vector<u8>& rgb)
{
    std::vector<u8> jpeg;
    REQUIRE(stbi_write_jpg_to_func(appendBytes, &jpeg, width, height, 3, rgb.data(), 95) != 0);
    return jpeg;
}

std::string base64(const std::vector<u8>& data)
{
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (usize i = 0; i < data.size(); i += 3)
    {
        const u32 triple = (u32(data[i]) << 16) | (i + 1 < data.size() ? u32(data[i + 1]) << 8 : 0u) |
                           (i + 2 < data.size() ? u32(data[i + 2]) : 0u);
        out += kTable[(triple >> 18) & 63];
        out += kTable[(triple >> 12) & 63];
        out += i + 1 < data.size() ? kTable[(triple >> 6) & 63] : '=';
        out += i + 2 < data.size() ? kTable[triple & 63] : '=';
    }
    return out;
}
} // namespace

TEST_CASE("Image: PNG round trip is exact, first row is the top")
{
    // 2x2: top row red, green; bottom row blue, half-transparent white.
    const std::vector<u8> pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};
    const auto png = encodePng(2, 2, 4, pixels);
    auto image = decodeImage(png, "test.png");
    REQUIRE_MESSAGE(image.ok(), (image.ok() ? "" : image.error().message));
    CHECK(image.value().width == 2);
    CHECK(image.value().height == 2);
    CHECK(image.value().rgba8 == pixels);
}

TEST_CASE("Image: grey and RGB sources become RGBA")
{
    auto grey = decodeImage(encodePng(1, 1, 1, {77}));
    REQUIRE(grey.ok());
    CHECK(grey.value().rgba8 == std::vector<u8>{77, 77, 77, 255});

    auto rgb = decodeImage(encodePng(1, 1, 3, {1, 2, 3}));
    REQUIRE(rgb.ok());
    CHECK(rgb.value().rgba8 == std::vector<u8>{1, 2, 3, 255});
}

TEST_CASE("Image: JPEG decodes approximately")
{
    std::vector<u8> rgb;
    for (int i = 0; i < 8 * 8; ++i)
    {
        rgb.insert(rgb.end(), {200, 100, 50});
    }
    auto image = decodeImage(encodeJpeg(8, 8, rgb), "test.jpg");
    REQUIRE(image.ok());
    const auto& p = image.value().rgba8;
    CHECK(std::abs(int(p[0]) - 200) <= 4);
    CHECK(std::abs(int(p[1]) - 100) <= 4);
    CHECK(std::abs(int(p[2]) - 50) <= 4);
    CHECK(p[3] == 255);
}

TEST_CASE("Image: errors")
{
    const std::vector<u8> garbage = {1, 2, 3, 4, 5, 6, 7, 8};
    auto broken = decodeImage(garbage, "broken.png");
    REQUIRE_FALSE(broken.ok());
    CHECK(broken.error().message.find("broken.png") != std::string::npos);
    CHECK_FALSE(decodeImage({}).ok());
    CHECK_FALSE(loadImage(fs::Path("does/not/exist.png")).ok());
}

TEST_CASE("Image: loadImage from a file")
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path file =
        std::filesystem::temp_directory_path() / ("g7_image_" + std::to_string(stamp) + ".png");
    REQUIRE(fs::writeFile(file, encodePng(1, 1, 4, {9, 8, 7, 6})).ok());
    auto image = loadImage(file);
    REQUIRE(image.ok());
    CHECK(image.value().rgba8 == std::vector<u8>{9, 8, 7, 6});
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

TEST_CASE("glTF: images from a data URI, a buffer view and a file URI")
{
    const auto png = encodePng(1, 1, 4, {10, 20, 30, 255});
    // Buffer: triangle positions (36 bytes), then the PNG for the buffer-view image.
    std::vector<u8> buffer(36, 0);
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::memcpy(buffer.data(), positions, 36);
    buffer.insert(buffer.end(), png.begin(), png.end());

    const std::string json =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],)"
        R"("materials":[{"name":"a","pbrMetallicRoughness":{"baseColorTexture":{"index":1}}}],)"
        R"("textures":[{"source":0},{"source":1},{"source":2}],)"
        R"("images":[{"uri":"data:image/png;base64,)" +
        base64(png) +
        R"("},)"
        R"({"bufferView":1,"mimeType":"image/png"},{"uri":"textures/stone.png"}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":)" +
        std::to_string(png.size()) +
        R"(}],)"
        R"("buffers":[{"byteLength":)" +
        std::to_string(buffer.size()) + R"(,"uri":"data:application/octet-stream;base64,)" + base64(buffer) +
        R"("}]})";

    auto mesh =
        loadGltf(std::span(reinterpret_cast<const u8*>(json.data()), json.size()), fs::Path("."), "img.gltf");
    REQUIRE_MESSAGE(mesh.ok(), (mesh.ok() ? "" : mesh.error().message));
    const MeshData& data = mesh.value();
    REQUIRE(data.images.size() == 3);
    CHECK(data.images[0].encoded == png);
    CHECK(data.images[0].mimeType == "image/png");
    CHECK(data.images[1].encoded == png);
    CHECK(data.images[2].uri == "textures/stone.png");
    CHECK(data.images[2].encoded.empty());
    REQUIRE(data.materials.size() == 1);
    CHECK(data.materials[0].baseColorImage == 1);

    auto decoded = decodeImage(data.images[1].encoded);
    REQUIRE(decoded.ok());
    CHECK(decoded.value().rgba8 == std::vector<u8>{10, 20, 30, 255});
}

TEST_CASE("Image: savePng writes what loadImage reads")
{
    const ImageData source{2, 1, {255, 0, 0, 255, 0, 0, 255, 128}};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::Path file =
        std::filesystem::temp_directory_path() / ("g7_save_" + std::to_string(stamp) + ".png");
    REQUIRE(savePng(file, source).ok());
    auto loaded = loadImage(file);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().width == 2);
    CHECK(loaded.value().rgba8 == source.rgba8);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);

    CHECK_FALSE(asset::encodePng(ImageData{2, 2, {1, 2, 3}}).ok()); // size mismatch
    CHECK_FALSE(asset::encodePng(ImageData{}).ok());
}

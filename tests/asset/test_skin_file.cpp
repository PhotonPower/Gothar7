// Cooked skinned models, ".g7skin" (ADR 0025, plan M17 cooked figures part B): round trips of real figure
// parts (morphs, neck and falloff data, garments with covers), the same assembled figure from cooked and
// glTF parts, and corrupt files refused without crashing.

#include <g7/asset/FigureAssembly.hpp>
#include <g7/asset/SkinFile.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
const std::filesystem::path kCharacters = std::filesystem::path(G7_ASSET_SOURCE_DIR) / "characters";

SkinnedModelData loadPart(const std::string& relative)
{
    const auto bytes = fs::readFile(kCharacters / relative);
    REQUIRE_MESSAGE(bytes.ok(), relative);
    auto model = loadSkinnedGltf(bytes.value(), {}, relative);
    REQUIRE_MESSAGE(model.ok(), (model.ok() ? std::string() : model.error().message));
    return std::move(model).value();
}

SkinnedModelData roundTrip(const SkinnedModelData& model)
{
    auto read = deserializeSkin(serializeSkin(model), "round trip");
    REQUIRE_MESSAGE(read.ok(), (read.ok() ? std::string() : read.error().message));
    return std::move(read).value();
}
} // namespace

TEST_CASE("g7skin: figure parts survive the round trip (morphs, neck, falloff, covers, hides)")
{
    for (const char* relative :
         {"parts/body_m_average/body.glb", "parts/head_m_farmer/head.glb", "parts/head_m_farmer/hair.glb",
          "parts/cloth_m_average/elvs_crude_t-shirt_male.glb", "parts/headgear_m_average/nasal_helmet.glb",
          "rig/human_reference.glb"})
    {
        CAPTURE(relative);
        const SkinnedModelData source = loadPart(relative);
        const std::vector<u8> bytes = serializeSkin(source);
        const SkinnedModelData read = roundTrip(source);
        CHECK(serializeSkin(read) == bytes); // nothing lost: writing again gives the same file
        CHECK(read.skeleton.names == source.skeleton.names);
        CHECK(read.skeleton.parents == source.skeleton.parents);
        REQUIRE(read.parts.size() == source.parts.size());
        for (usize i = 0; i < read.parts.size(); ++i)
        {
            const SkinnedPartData& a = read.parts[i];
            const SkinnedPartData& b = source.parts[i];
            CHECK(a.node == b.node);
            CHECK(a.vertices.size() == b.vertices.size());
            CHECK(a.indices == b.indices);
            CHECK(a.joints == b.joints);
            CHECK(a.morphs.size() == b.morphs.size());
            CHECK(a.primitives.size() == b.primitives.size());
        }
        CHECK(read.assembly.version == source.assembly.version);
        CHECK(read.assembly.neck.size() == source.assembly.neck.size());
        CHECK(read.assembly.falloff.size() == source.assembly.falloff.size());
        CHECK(read.assembly.covers.size() == source.assembly.covers.size());
        CHECK(read.assembly.hides == source.assembly.hides);
        CHECK(read.materials.size() == source.materials.size());
        CHECK(read.images.size() == source.images.size());
    }
    // What the parts carry, so the loop above covers it.
    CHECK_FALSE(loadPart("parts/head_m_farmer/head.glb").parts.front().morphs.empty());
    CHECK_FALSE(loadPart("parts/body_m_average/body.glb").assembly.falloff.empty());
    CHECK_FALSE(loadPart("parts/cloth_m_average/elvs_crude_t-shirt_male.glb").assembly.covers.empty());
}

TEST_CASE("g7skin: a figure assembled from cooked parts equals the one from glTF parts")
{
    const auto toml = fs::readText(kCharacters / "figures/farmer.figure.toml");
    REQUIRE(toml.ok());
    auto manifest = FigureManifest::parse(toml.value(), "farmer.figure.toml");
    REQUIRE(manifest.ok());
    std::vector<SkinnedModelData> gltf;
    std::vector<SkinnedModelData> cooked;
    gltf.reserve(manifest.value().parts.size());
    cooked.reserve(manifest.value().parts.size());
    std::vector<FigurePart> fromGltf;
    std::vector<FigurePart> fromCooked;
    for (const FigureManifest::Part& p : manifest.value().parts)
    {
        gltf.push_back(loadPart(p.path));
        cooked.push_back(roundTrip(gltf.back()));
        fromGltf.push_back({p.role, "characters/" + p.path, &gltf.back()});
        fromCooked.push_back({p.role, "characters/" + p.path, &cooked.back()});
    }
    auto a = assembleFigure(manifest.value(), fromGltf);
    auto b = assembleFigure(manifest.value(), fromCooked);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(serializeSkin(a.value()) == serializeSkin(b.value()));
}

TEST_CASE("g7skin: corrupt, truncated and foreign files are refused")
{
    const std::vector<u8> bytes = serializeSkin(loadPart("parts/head_m_farmer/head.glb"));
    REQUIRE(deserializeSkin(bytes).ok());

    std::vector<u8> magic = bytes;
    magic[0] = 'X';
    CHECK_FALSE(deserializeSkin(magic).ok());

    std::vector<u8> version = bytes;
    version[4] = 99;
    auto foreign = deserializeSkin(version, "head.g7skin");
    REQUIRE_FALSE(foreign.ok());
    CHECK(foreign.error().message.find("version 99") != std::string::npos);

    for (const usize cut : {usize(0), usize(7), usize(9), usize(100), bytes.size() / 3, bytes.size() / 2,
                            bytes.size() - 5, bytes.size() - 1})
    {
        CAPTURE(cut);
        CHECK_FALSE(deserializeSkin(std::span(bytes).first(cut)).ok());
    }
    std::vector<u8> trailing = bytes;
    trailing.push_back(0);
    CHECK_FALSE(deserializeSkin(trailing).ok());

    // A huge bone count must not allocate: refused against the size of the file.
    std::vector<u8> bones = bytes;
    bones[8] = 0xff;
    bones[9] = 0xff;
    CHECK_FALSE(deserializeSkin(bones).ok());

    // Random damage in the body never crashes (it may still parse if only floats were hit).
    for (usize at = 16; at < bytes.size(); at += bytes.size() / 97 + 1)
    {
        std::vector<u8> damaged = bytes;
        damaged[at] ^= 0xa5;
        static_cast<void>(deserializeSkin(damaged));
    }
}

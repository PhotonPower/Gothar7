// Figures assembled at run time (M6 part D2): manifest parsing, part data from asset.extras.gothar, and the
// C++ assembly compared with `gothar-chargen assemble` (the figures g7_figures builds). Configured with
// -DG7_REQUIRE_FIGURES=ON (CI), missing figures are an error instead of a skip. A figure built from older
// parts (its asset.extras.gothar.inputs hash differs, figuren's algorithm) is reported as outdated, not
// compared.

#include <g7/asset/FigureAssembly.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <ostream> // doctest needs it to print std::string operands
#include <span>
#include <string>
#include <vector>

using namespace g7;
using namespace g7::asset;

namespace
{
const std::filesystem::path kCharacters = std::filesystem::path(G7_ASSET_SOURCE_DIR) / "characters";

std::string text(const std::filesystem::path& path)
{
    auto bytes = fs::readFile(path);
    REQUIRE_MESSAGE(bytes.ok(), path.generic_string());
    return std::string(bytes.value().begin(), bytes.value().end());
}

SkinnedModelData load(const std::filesystem::path& path)
{
    auto bytes = fs::readFile(path);
    REQUIRE_MESSAGE(bytes.ok(), path.generic_string());
    auto model = loadSkinnedGltf(bytes.value(), {}, path.filename().generic_string());
    REQUIRE_MESSAGE(model.ok(), (model.ok() ? "" : model.error().message));
    return std::move(model).value();
}

/// "../textures/x.jpg" next to characters/figures/<name>.glb -> "characters/textures/x.jpg".
std::string figureImage(const std::string& uri)
{
    std::filesystem::path p = std::filesystem::path("characters/figures") / uri;
    return p.lexically_normal().generic_string();
}

/// SHA-256 (FIPS 180-4), for the figures' inputs hash.
class Sha256
{
public:
    void update(std::span<const u8> data)
    {
        for (const u8 b : data)
        {
            m_block[m_used++] = b;
            m_bits += 8;
            if (m_used == 64)
            {
                compress();
                m_used = 0;
            }
        }
    }
    void update(std::string_view s) { update(std::span(reinterpret_cast<const u8*>(s.data()), s.size())); }
    [[nodiscard]] std::string hex()
    {
        const u64 bits = m_bits;
        const u8 one = 0x80;
        update(std::span(&one, 1));
        const u8 zero = 0;
        while (m_used != 56)
        {
            update(std::span(&zero, 1));
        }
        for (int i = 7; i >= 0; --i)
        {
            const auto b = static_cast<u8>(bits >> (i * 8));
            update(std::span(&b, 1));
        }
        std::string out;
        for (const u32 h : m_h)
        {
            out += std::format("{:08x}", h);
        }
        return out;
    }

private:
    static u32 rotr(u32 x, int n) { return (x >> n) | (x << (32 - n)); }
    void compress()
    {
        static constexpr std::array<u32, 64> k = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::array<u32, 64> w{};
        for (int i = 0; i < 16; ++i)
        {
            w[i] = u32(m_block[i * 4]) << 24 | u32(m_block[i * 4 + 1]) << 16 | u32(m_block[i * 4 + 2]) << 8 |
                   u32(m_block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i)
        {
            const u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = m_h;
        for (int i = 0; i < 64; ++i)
        {
            const u32 t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const u32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        const std::array<u32, 8> add = {a, b, c, d, e, f, g, h};
        for (int i = 0; i < 8; ++i)
        {
            m_h[i] += add[i];
        }
    }
    std::array<u32, 8> m_h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<u8, 64> m_block{};
    usize m_used = 0;
    u64 m_bits = 0;
};

/// figuren's inputs hash (gothar-chargen assemble): SHA-256 over the manifest's bytes, then per role in ASCII
/// order the role name and the part file's bytes; the first 16 hex digits.
std::string inputsHash(const std::filesystem::path& manifest, const FigureManifest& figure)
{
    Sha256 sha;
    sha.update(text(manifest));
    std::vector<const FigureManifest::Part*> parts;
    for (const FigureManifest::Part& p : figure.parts)
    {
        parts.push_back(&p);
    }
    std::ranges::sort(parts, {}, [](const FigureManifest::Part* p) { return p->role; });
    for (const FigureManifest::Part* p : parts)
    {
        sha.update(p->role);
        sha.update(text(kCharacters / p->path));
    }
    return sha.hex().substr(0, 16);
}

/// asset.extras.gothar.inputs of a built figure (the glTF JSON chunk of the .glb); empty if it has none.
std::string recordedInputs(const std::filesystem::path& glb)
{
    const std::string bytes = text(glb);
    if (bytes.size() < 20)
    {
        return {};
    }
    u32 length = 0;
    std::memcpy(&length, bytes.data() + 12, 4);
    const std::string_view json(bytes.data() + 20, std::min<usize>(length, bytes.size() - 20));
    constexpr std::string_view kKey = "\"inputs\":\"";
    const usize at = json.find(kKey);
    if (at == std::string_view::npos)
    {
        return {};
    }
    const usize end = json.find('"', at + kKey.size());
    return std::string(json.substr(at + kKey.size(), end - at - kKey.size()));
}

bool requireFigures()
{
#ifdef G7_REQUIRE_FIGURES
    return true;
#else
    return false;
#endif
}
} // namespace

TEST_CASE("Figure manifest: roles, garments, palette, errors, editing")
{
    auto m = FigureManifest::parse(R"(
version = 1
[parts]
hair = "parts/h/hair.glb"
head = "parts/h/head.glb"
body = "parts/b/body.glb"
cloth = ["parts/c/Elvs Crude_T-shirt.glb", "parts/c/boots.glb"]
[palette]
cloth_boots = "#5a4232"
)",
                                   "m.figure.toml");
    REQUIRE_MESSAGE(m.ok(), (m.ok() ? "" : m.error().message));
    const FigureManifest& f = m.value();
    REQUIRE(f.parts.size() == 5);
    CHECK(f.parts[0].role == "body"); // fixed order: body, head, hair, beard, garments
    CHECK(f.parts[1].role == "head");
    CHECK(f.parts[2].role == "hair");
    CHECK(f.parts[3].role == "cloth_elvs_crude_t_shirt");
    CHECK(f.parts[4].path == "parts/c/boots.glb");
    REQUIRE(f.palette.size() == 1);
    CHECK(f.palette[0].second.x == doctest::Approx(0.102242f).epsilon(1e-4)); // sRGB 0x5a -> linear
    CHECK(clothRole("parts/x/--Mail  Tunic--.glb") == "cloth_mail_tunic");

    FigureManifest edited = f;
    CHECK(edited.setPart("beard", "parts/h/beard.glb").ok());
    CHECK(edited.parts[3].role == "beard"); // before the garments
    CHECK(edited.setPart("hair", "").ok());
    CHECK(edited.find("hair") == nullptr);
    CHECK_FALSE(edited.setPart("head", "").ok());
    CHECK_FALSE(edited.setPart("cape", "parts/c/cape.glb").ok());
    edited.setCloth(std::vector<std::string>{"parts/a/mail_tunic.glb"});
    REQUIRE(edited.cloth().size() == 1);
    CHECK(edited.find("cloth_mail_tunic") != nullptr);
    CHECK(edited.find("cloth_boots") == nullptr);

    const auto fails = [](const char* toml, const char* expected)
    {
        auto r = FigureManifest::parse(toml, "x.figure.toml");
        REQUIRE_FALSE(r.ok());
        CHECK_MESSAGE(r.error().message.find(expected) != std::string::npos, r.error().message);
    };
    fails("[parts]\nbody = \"a.glb\"\n", "version = 1");
    fails("version = 1\n[parts]\nbody = \"a.glb\"\n", "'head'");
    fails("version = 1\n[parts]\nbody = \"a.glb\"\nhead = \"/abs/h.glb\"\n", "relative .glb");
    fails("version = 1\n[parts]\nbody = \"a.glb\"\nhead = \"h.glb\"\ncape = \"c.glb\"\n",
          "unknown part role");
    fails("version = 1\n[parts]\nbody = \"a.glb\"\nhead = \"h.glb\"\n[palette]\nskin = \"red\"\n", "#rrggbb");
}

TEST_CASE("Figure parts carry their assembly data (asset.extras.gothar)")
{
    const SkinnedModelData body = load(kCharacters / "parts/body_m_average/body.glb");
    CHECK(body.assembly.version == 1);
    CHECK(body.assembly.part == "body");
    REQUIRE(body.assembly.neck.contains("body_lod0"));
    CHECK(body.assembly.neck.at("body_lod0").size() > 10);
    CHECK_FALSE(body.assembly.falloff.at("body_lod0").empty());
    REQUIRE_FALSE(body.parts.empty());
    usize triangles = 0;
    for (const SkinnedPartData::Primitive& p : body.parts.front().primitives)
    {
        triangles += p.indexCount / 3;
    }
    CHECK(triangles * 3 == body.parts.front().indices.size()); // every triangle in one primitive range

    const SkinnedModelData shirt = load(kCharacters / "parts/cloth_m_average/elvs_crude_t-shirt_male.glb");
    CHECK(shirt.assembly.coversBody == "parts/body_m_average/body.glb");
    CHECK(shirt.assembly.covers.contains("body_lod0"));
    const SkinnedModelData helmet = load(kCharacters / "parts/headgear_m_average/nasal_helmet.glb");
    CHECK(helmet.assembly.hides == std::vector<std::string>{"hair"});
    // Files without part data (figures, animation rigs) have none.
    CHECK(load(kCharacters / "figures/placeholder_mannequin.glb").assembly.version == 0);
}

TEST_CASE("Figure assembly in C++ matches gothar-chargen assemble (all figure manifests)")
{
    {
        Sha256 abc; // FIPS 180-4 test vector
        abc.update("abc");
        CHECK(abc.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        Sha256 empty;
        CHECK(empty.hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }
    usize compared = 0;
    for (const auto& entry : std::filesystem::directory_iterator(kCharacters / "figures"))
    {
        const std::string file = entry.path().filename().generic_string();
        if (!file.ends_with(".figure.toml"))
        {
            continue;
        }
        const std::string name = file.substr(0, file.size() - std::string(".figure.toml").size());
        CAPTURE(name);
        const std::filesystem::path python = kCharacters / "figures" / (name + ".glb");
        if (!std::filesystem::exists(python))
        {
            if (requireFigures())
            {
                FAIL_CHECK("figure not assembled (cmake --build --target g7_figures): " << name);
            }
            continue;
        }
        auto manifest = FigureManifest::parse(text(entry.path()), file);
        REQUIRE_MESSAGE(manifest.ok(), (manifest.ok() ? "" : manifest.error().message));
        // Built from older parts or an older manifest: no point comparing vertex by vertex.
        if (const std::string recorded = recordedInputs(python);
            !recorded.empty() && recorded != inputsHash(entry.path(), manifest.value()))
        {
            FAIL_CHECK("figure outdated (its parts or manifest changed since it was built) - rebuild it: "
                       "cmake --build --preset debug --target g7_figures: "
                       << name);
            continue;
        }
        std::vector<SkinnedModelData> loaded;
        loaded.reserve(manifest.value().parts.size());
        std::vector<FigurePart> parts;
        for (const FigureManifest::Part& p : manifest.value().parts)
        {
            loaded.push_back(load(kCharacters / p.path));
            parts.push_back({p.role, "characters/" + p.path, &loaded.back()});
        }
        auto assembled = assembleFigure(manifest.value(), parts);
        REQUIRE_MESSAGE(assembled.ok(), (assembled.ok() ? "" : assembled.error().message));
        const SkinnedModelData& cpp = assembled.value();
        const SkinnedModelData py = load(python);

        CHECK(cpp.skeleton.names == py.skeleton.names);
        // Materials by name: colour factor and images.
        REQUIRE(cpp.materials.size() >= py.materials.size());
        std::map<std::string, const MaterialInfo*> cppMaterials;
        for (const MaterialInfo& m : cpp.materials)
        {
            cppMaterials[m.name] = &m;
        }
        const auto imageOf = [](const SkinnedModelData& model, i32 index, bool python)
        {
            if (index < 0)
            {
                return std::string();
            }
            const std::string& uri = model.images[static_cast<usize>(index)].uri;
            return python ? figureImage(uri) : uri;
        };
        for (const MaterialInfo& m : py.materials)
        {
            CAPTURE(m.name);
            REQUIRE(cppMaterials.contains(m.name));
            const MaterialInfo& c = *cppMaterials[m.name];
            CHECK(glm::length(c.baseColor - m.baseColor) < 1e-6f);
            CHECK(imageOf(cpp, c.baseColorImage, false) == imageOf(py, m.baseColorImage, true));
            CHECK(imageOf(cpp, c.normalImage, false) == imageOf(py, m.normalImage, true));
            CHECK(c.alphaMode == m.alphaMode);
        }
        // Parts: same nodes, vertices, joints (by bone name), triangles per material name.
        REQUIRE(cpp.parts.size() == py.parts.size());
        for (const SkinnedPartData& p : py.parts)
        {
            CAPTURE(p.node);
            const auto found = std::find_if(cpp.parts.begin(), cpp.parts.end(),
                                            [&](const SkinnedPartData& c) { return c.node == p.node; });
            REQUIRE(found != cpp.parts.end());
            const SkinnedPartData& c = *found;
            REQUIRE(c.vertices.size() == p.vertices.size());
            f32 worst = 0.0f;
            bool attributes = true;
            for (usize v = 0; v < p.vertices.size(); ++v)
            {
                worst = std::max(worst, glm::length(c.vertices[v].position - p.vertices[v].position));
                attributes = attributes && glm::length(c.vertices[v].normal - p.vertices[v].normal) < 1e-5f &&
                             glm::length(c.vertices[v].uv - p.vertices[v].uv) < 1e-6f &&
                             glm::length(c.weights[v] - p.weights[v]) < 1e-6f;
                for (usize j = 0; j < 4; ++j)
                {
                    attributes = attributes &&
                                 (p.weights[v][static_cast<glm::length_t>(j)] == 0.0f ||
                                  cpp.skeleton.names[c.joints[v][j]] == py.skeleton.names[p.joints[v][j]]);
                }
            }
            CHECK(worst < 1e-6f); // the neck snap computes in double like assemble.py
            CHECK(attributes);
            CHECK(c.morphs.size() == p.morphs.size());
            REQUIRE(c.submeshes.size() == p.submeshes.size());
            for (usize s = 0; s < p.submeshes.size(); ++s)
            {
                const Submesh& cs = c.submeshes[s];
                const Submesh& ps = p.submeshes[s];
                CHECK(cpp.materials[cs.material].name == py.materials[ps.material].name);
                REQUIRE(cs.indexCount == ps.indexCount);
                CHECK(std::equal(c.indices.begin() + cs.firstIndex,
                                 c.indices.begin() + cs.firstIndex + cs.indexCount,
                                 p.indices.begin() + ps.firstIndex));
            }
        }
        ++compared;
    }
    if (compared == 0)
    {
        MESSAGE("no assembled figures (build g7_figures): nothing compared");
    }
    else
    {
        MESSAGE(compared << " figures compared with gothar-chargen assemble");
    }
}

TEST_CASE("Figure assembly: errors")
{
    auto manifest =
        FigureManifest::parse(text(kCharacters / "figures/farmer.figure.toml"), "farmer.figure.toml");
    REQUIRE(manifest.ok());
    std::vector<SkinnedModelData> loaded;
    loaded.reserve(manifest.value().parts.size());
    std::vector<FigurePart> parts;
    for (const FigureManifest::Part& p : manifest.value().parts)
    {
        loaded.push_back(load(kCharacters / p.path));
        parts.push_back({p.role, "characters/" + p.path, &loaded.back()});
    }
    REQUIRE(assembleFigure(manifest.value(), parts).ok());

    // A part missing, a garment fitted to another body, a head without neck data.
    std::vector<FigurePart> noHair(parts.begin(), parts.end());
    std::erase_if(noHair, [](const FigurePart& p) { return p.role == "hair"; });
    CHECK_FALSE(assembleFigure(manifest.value(), noHair).ok());
    FigureManifest otherBody = manifest.value();
    REQUIRE(otherBody.setPart("body", "parts/body_m_heavy/body.glb").ok());
    const SkinnedModelData heavy = load(kCharacters / "parts/body_m_heavy/body.glb");
    std::vector<FigurePart> heavyParts(parts.begin(), parts.end());
    heavyParts[0] = {"body", "characters/parts/body_m_heavy/body.glb", &heavy};
    auto wrongFit = assembleFigure(otherBody, heavyParts);
    REQUIRE_FALSE(wrongFit.ok());
    CHECK(wrongFit.error().message.find("fitted to") != std::string::npos);
    SkinnedModelData bareHead = *parts[1].data;
    bareHead.assembly = {};
    std::vector<FigurePart> bare(parts.begin(), parts.end());
    bare[1].data = &bareHead;
    CHECK_FALSE(assembleFigure(manifest.value(), bare).ok());
}

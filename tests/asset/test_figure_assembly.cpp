// Figures assembled at run time (M6 part D2): manifest parsing, part data from asset.extras.gothar, and the
// C++ assembly compared with `gothar-chargen assemble` (the figures g7_figures builds). Configured with
// -DG7_REQUIRE_FIGURES=ON (CI), missing figures are an error instead of a skip.

#include <g7/asset/FigureAssembly.hpp>
#include <g7/asset/SkinnedModel.hpp>
#include <g7/core/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <ostream> // doctest needs it to print std::string operands
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

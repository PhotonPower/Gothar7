// Inventory cells on the GPU (M14 part D): a model rendered into its atlas tile by the contract's cell
// geometry is centred and fills `fill` of the tile (label "gpu").

#include "GlFixture.hpp"

#include <g7/asset/Procedural.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/ui/ItemPreview.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace g7;
using namespace g7::render;
using g7::test::GlFixture;
using g7::test::require;

TEST_CASE("Item previews GPU: the model is centred and fills its cell")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    constexpr u32 kTile = 64;
    ui::ItemPreviews previews = require(ui::ItemPreviews::create(*gl.device, library, kTile, 2));

    // A box 0.6 x 1.2 x 0.3, seen like an item in the default view.
    const asset::MeshData data = asset::makeBox(Vec3(0.3f, 0.6f, 0.15f), Vec4(0.8f, 0.6f, 0.4f, 1.0f));
    Mesh mesh = require(Mesh::create(*gl.device, data));
    MaterialSet materials = require(MaterialSet::create(*gl.device, data, fs::Path(".")));
    std::vector<Vec3> positions;
    for (const asset::Vertex& v : data.vertices)
    {
        positions.push_back(v.position);
    }
    const ui::InventoryView view{25.0f, 15.0f, 0.0f, 0.75f};
    const ui::CellFrame frame = require(ui::computeCell(positions, view, 30.0f));
    previews.render(*gl.device, renderer, 3, mesh, materials, frame, 0.0f, ui::PreviewLight{});
    gl.device->bindFramebuffer(nullptr);

    // Tile 3 is the upper right quarter of the atlas in GL rows (row 0 at the bottom).
    const std::vector<u8> pixels = gl.device->readTexture(previews.texture(), 0);
    REQUIRE(pixels.size() == 4u * kTile * 2 * kTile * 2);
    const auto alpha = [&](u32 x, u32 y) { return pixels[(static_cast<usize>(y) * kTile * 2 + x) * 4 + 3]; };
    u32 minX = kTile * 2;
    u32 minY = kTile * 2;
    u32 maxX = 0;
    u32 maxY = 0;
    usize outside = 0;
    for (u32 y = 0; y < kTile * 2; ++y)
    {
        for (u32 x = 0; x < kTile * 2; ++x)
        {
            if (alpha(x, y) < 128)
            {
                continue;
            }
            if (x < kTile || y < kTile)
            {
                ++outside; // another tile
                continue;
            }
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }
    CHECK(outside == 0);
    REQUIRE(maxX > minX);
    const f32 width = static_cast<f32>(maxX - minX + 1);
    const f32 height = static_cast<f32>(maxY - minY + 1);
    const f32 larger = std::max(width, height);
    CHECK(larger == doctest::Approx(0.75f * kTile).epsilon(0.06)); // fill 0.75 of the tile
    const f32 centreX = (static_cast<f32>(minX + maxX) + 1.0f) * 0.5f - static_cast<f32>(kTile);
    const f32 centreY = (static_cast<f32>(minY + maxY) + 1.0f) * 0.5f - static_cast<f32>(kTile);
    CHECK(std::abs(centreX - kTile * 0.5f) <= 1.5f); // centred on the contour
    CHECK(std::abs(centreY - kTile * 0.5f) <= 1.5f);
    // The uv of the tile points at it with the image's top first.
    const ui::Rect uv = previews.uv(3);
    CHECK(uv.min.x == doctest::Approx(0.5f));
    CHECK(uv.min.y == doctest::Approx(1.0f));
    CHECK(uv.max.y == doctest::Approx(0.5f));
    CHECK(gl.device->debugErrorCount() == 0);
}

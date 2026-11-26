// Window glass on the GPU (M17 windows F1, label "gpu"): a pane marked as glass mirrors a bright sky - weakly
// head-on, strongly at grazing views; with window reflections off it is byte-equal to the plain material; the
// batched path draws it like single draws. F1b: at night a light behind the pane glows warm through it - by
// chance in houses without rooms, by its own lights in a room of the world.

#include "GlFixture.hpp"

#include <g7/render/Camera.hpp>
#include <g7/render/GeometryArena.hpp>
#include <g7/render/Glass.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <utility>
#include <vector>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr i32 kSize = 64;

/// A dark 8 x 8 m pane in the XY plane facing +Z; `glass` marks its material.
asset::MeshData pane(bool glass, asset::GlassPattern pattern = asset::GlassPattern::Plain)
{
    asset::MeshData data;
    for (const Vec2 p : {Vec2(-4.0f, -4.0f), Vec2(4.0f, -4.0f), Vec2(4.0f, 4.0f), Vec2(-4.0f, 4.0f)})
    {
        asset::Vertex v{};
        v.position = Vec3(p, 0.0f);
        v.normal = Vec3(0.0f, 0.0f, 1.0f);
        data.vertices.push_back(v);
    }
    data.indices = {0, 1, 2, 0, 2, 3};
    data.submeshes = {{0, 6, 0}};
    asset::MaterialInfo material;
    material.baseColor = Vec4(0.05f, 0.05f, 0.05f, 1.0f);
    material.glass = glass;
    material.glassPattern = pattern;
    data.materials.push_back(material);
    data.bounds = AABB{Vec3(-4.0f, -4.0f, 0.0f), Vec3(4.0f, 4.0f, 0.0f)};
    return data;
}

/// Sum of the red, green and blue bytes of an image.
u64 brightness(const std::vector<u8>& pixels)
{
    u64 sum = 0;
    for (usize i = 0; i < pixels.size(); i += 4)
    {
        sum += u64(pixels[i]) + pixels[i + 1] + pixels[i + 2];
    }
    return sum;
}

struct Scene
{
    GlFixture gl;
    ShaderLibrary library{*gl.device, fs::fromUtf8(G7_SHADER_DIR)};
    MeshRenderer renderer = require(MeshRenderer::create(*gl.device, library, 1.0f));
    GeometryArena arena;
    Texture color = require(gl.device->createTexture({kSize, kSize, Format::RGBA8, 1}));
    Texture depth = require(gl.device->createTexture({kSize, kSize, Format::Depth32F, 1}));
    Framebuffer target = require(gl.device->createFramebuffer({{&color}, &depth}));
    Camera camera;
    LightList none;
    /// No light on the pane itself: what it shows is the mirrored sky (white at the horizon, blue above).
    Environment environment{.sunIntensity = 0.0f,
                            .ambientSky = Vec3(0.0f),
                            .ambientGround = Vec3(0.0f),
                            .fogColor = Vec3(0.9f),
                            .fogStart = 1000.0f,
                            .fogDensity = 0.0f,
                            .skyZenith = Vec3(0.3f, 0.5f, 0.9f)};

    Scene()
    {
        camera.aspect = 1.0f;
        camera.transform.position = Vec3(0.0f, 0.0f, 3.0f);
    }

    void begin(WindowReflections quality)
    {
        gl.device->bindFramebuffer(&target);
        gl.device->setViewport(0, 0, kSize, kSize);
        gl.device->clear(Vec4(0, 0, 0, 1), 0.0f);
        environment.windowReflections = quality;
        renderer.setLighting(*gl.device, environment, none);
    }

    std::vector<u8> draw(const asset::MeshData& data, const Mat4& model, WindowReflections quality,
                         const LightList* lights = nullptr)
    {
        const Mesh mesh = require(Mesh::create(*gl.device, data));
        const MaterialSet materials = require(MaterialSet::create(*gl.device, data, {}, renderer.defaults()));
        begin(quality);
        if (lights != nullptr)
        {
            renderer.setLighting(*gl.device, environment, *lights);
        }
        renderer.draw(*gl.device, mesh, materials, model, camera);
        return gl.device->readPixels(0, 0, kSize, kSize, &target);
    }
};
} // namespace

TEST_CASE("Glass GPU: a pane mirrors the sky, weakly head-on and strongly at grazing views")
{
    Scene s;
    const Mat4 headOn(1.0f);
    // Turned 75 degrees about +Y: the camera sees the pane at a grazing angle (still filling the middle).
    const Mat4 grazing = glm::rotate(Mat4(1.0f), glm::radians(75.0f), Vec3(0.0f, 1.0f, 0.0f));

    const u64 plainHeadOn = brightness(s.draw(pane(false), headOn, WindowReflections::Low));
    const u64 glassHeadOn = brightness(s.draw(pane(true), headOn, WindowReflections::Low));
    const u64 plainGrazing = brightness(s.draw(pane(false), grazing, WindowReflections::Low));
    const u64 glassGrazing = brightness(s.draw(pane(true), grazing, WindowReflections::Low));
    CHECK(plainHeadOn == 0); // unlit, dark
    CHECK(glassHeadOn > 0);  // the sky, faintly (about 4 %)
    CHECK(glassGrazing > plainGrazing);
    // Per covered pixel the grazing view mirrors far more; the grazing pane covers fewer pixels, so compare
    // per pixel.
    const auto covered = [&](const Mat4& model)
    {
        asset::MeshData white = pane(false);
        white.materials[0].emissive = Vec3(1.0f);
        usize n = 0;
        const auto pixels = s.draw(white, model, WindowReflections::Low);
        for (usize i = 0; i < pixels.size(); i += 4)
        {
            n += pixels[i] > 128 ? 1 : 0;
        }
        return n;
    };
    const usize headOnArea = covered(headOn);
    const usize grazingArea = covered(grazing);
    REQUIRE(headOnArea > 0);
    REQUIRE(grazingArea > 0);
    const f64 perPixelHeadOn = f64(glassHeadOn) / f64(headOnArea);
    const f64 perPixelGrazing = f64(glassGrazing) / f64(grazingArea);
    CHECK(perPixelGrazing >
          3.0 * perPixelHeadOn); // about 3.7: the near part of the pane is seen less grazing
    // Head-on discreet: well below half of the sky's brightness (0.9 -> 3 x 230 per pixel).
    CHECK(perPixelHeadOn < 0.15 * 3.0 * 230.0);

    // Every pattern draws (bullseye and diamond panes with their lead).
    for (const asset::GlassPattern pattern : {asset::GlassPattern::Bullseye, asset::GlassPattern::Diamond})
    {
        CHECK(brightness(s.draw(pane(true, pattern), grazing, WindowReflections::Low)) > plainGrazing);
    }
    CHECK(s.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Glass GPU: with window reflections off a glass pane is byte-equal to the plain material")
{
    Scene s;
    s.environment.ambientSky = Vec3(0.6f); // lit, so the equality means something
    s.environment.ambientGround = Vec3(0.3f);
    const Mat4 grazing = glm::rotate(Mat4(1.0f), glm::radians(60.0f), Vec3(0.0f, 1.0f, 0.0f));
    const auto plain = s.draw(pane(false), grazing, WindowReflections::Off);
    CHECK(brightness(plain) > 0);
    CHECK(s.draw(pane(true, asset::GlassPattern::Bullseye), grazing, WindowReflections::Off) == plain);
    CHECK(s.draw(pane(true), grazing, WindowReflections::Low) != plain);
    CHECK(s.gl.device->debugErrorCount() == 0);
}

TEST_CASE("Glass GPU: batched draws show glass like single draws, in a group of their own")
{
    Scene s;
    const asset::MeshData glassData = pane(true, asset::GlassPattern::Diamond);
    const asset::MeshData plainData = pane(false);
    const Mesh glass = require(Mesh::create(*s.gl.device, s.arena, glassData));
    const Mesh plain = require(Mesh::create(*s.gl.device, s.arena, plainData));
    const MaterialSet glassMaterials =
        require(MaterialSet::create(*s.gl.device, glassData, {}, s.renderer.defaults()));
    const MaterialSet plainMaterials =
        require(MaterialSet::create(*s.gl.device, plainData, {}, s.renderer.defaults()));
    const Mat4 left = glm::translate(Mat4(1.0f), Vec3(-4.0f, 0.0f, -2.0f)) *
                      glm::rotate(Mat4(1.0f), glm::radians(60.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 right = glm::translate(Mat4(1.0f), Vec3(4.0f, 0.0f, -2.0f));

    s.begin(WindowReflections::Low);
    s.renderer.draw(*s.gl.device, glass, glassMaterials, left, s.camera);
    s.renderer.draw(*s.gl.device, plain, plainMaterials, right, s.camera);
    const auto single = s.gl.device->readPixels(0, 0, kSize, kSize, &s.target);

    const std::vector<MeshDrawItem> items{
        {&glass, &glassMaterials, left, glass.bounds().transformed(left)},
        {&plain, &plainMaterials, right, plain.bounds().transformed(right)}};
    s.renderer.beginFrame();
    s.begin(WindowReflections::Low);
    s.renderer.drawBatched(*s.gl.device, items, s.camera);
    CHECK(s.gl.device->readPixels(0, 0, kSize, kSize, &s.target) == single);
    CHECK(s.renderer.lastBatch().groups == 2); // glass does not share the plain pane's group
    CHECK(s.gl.device->debugErrorCount() == 0);
}

namespace
{
/// Sum of red and blue over an image: a warm glow has far more red.
std::pair<u64, u64> redBlue(const std::vector<u8>& pixels)
{
    u64 red = 0;
    u64 blue = 0;
    for (usize i = 0; i < pixels.size(); i += 4)
    {
        red += pixels[i];
        blue += pixels[i + 2];
    }
    return {red, blue};
}

/// A night: dark sky, no sun; `awake` the share of lit window rows.
void night(Scene& s, f32 awake)
{
    s.environment.fogColor = Vec3(0.01f);
    s.environment.skyZenith = Vec3(0.005f, 0.006f, 0.012f);
    s.environment.windowsAwake = awake;
    s.environment.windowGlowNight = 1.0f;
}
} // namespace

TEST_CASE("Glass GPU: at night a pane glows warm when its window row is awake, not by day (F1b)")
{
    Scene s;
    night(s, 1.0f); // every window row lit
    const auto lit = s.draw(pane(true), Mat4(1.0f), WindowReflections::Low);
    const auto [red, blue] = redBlue(lit);
    CHECK(red > 2 * blue); // warm
    CHECK(red > u64(kSize) * kSize * 20);

    night(s, 0.0f); // all asleep
    const u64 asleep = brightness(s.draw(pane(true), Mat4(1.0f), WindowReflections::Low));
    CHECK(asleep * 4 < brightness(lit));

    night(s, 1.0f);
    s.environment.windowGlowNight = 0.0f; // daylight hides it
    CHECK(brightness(s.draw(pane(true), Mat4(1.0f), WindowReflections::Low)) == asleep);
    s.environment.windowGlowNight = 1.0f;
    // With window reflections off there is no glass and no glow.
    CHECK(brightness(s.draw(pane(true), Mat4(1.0f), WindowReflections::Off)) == 0);
    CHECK(s.gl.device->debugErrorCount() == 0);
}

TEST_CASE(
    "Glass GPU: in a room of the world the pane glows by the room's light behind it, not by chance (F1b)")
{
    Scene s;
    night(s, 1.0f); // chance alone would light it: in a room only its lights count
    // The room behind the pane (z < 0), its wall in the pane's plane.
    s.environment.indoor = {
        IndoorVolume{.center = Vec3(0.0f, 0.0f, -2.5f), .halfExtents = Vec3(4.0f, 4.0f, 2.0f)}};
    LightList dark;
    const u64 unlit = brightness(s.draw(pane(true), Mat4(1.0f), WindowReflections::Low, &dark));

    LightList candle;
    candle.add(
        PointLight{.position = Vec3(0.0f, 0.0f, -1.5f), .radius = 5.0f, .color = Vec3(1.0f, 0.6f, 0.3f)});
    const auto lit = s.draw(pane(true), Mat4(1.0f), WindowReflections::Low, &candle);
    const auto [red, blue] = redBlue(lit);
    CHECK(brightness(lit) > 4 * unlit);
    CHECK(red > 2 * blue);

    // A light in front of the window (in the street) does not glow through it.
    LightList street;
    street.add(
        PointLight{.position = Vec3(0.0f, 0.0f, 1.5f), .radius = 5.0f, .color = Vec3(1.0f, 0.6f, 0.3f)});
    const auto [streetRed, streetBlue] =
        redBlue(s.draw(pane(true), Mat4(1.0f), WindowReflections::Low, &street));
    CHECK(streetRed < red / 2);
    CHECK(s.gl.device->debugErrorCount() == 0);
}

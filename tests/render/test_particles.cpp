// Particles (M12 part A): emitter definitions (data/fx), the CPU simulation - rate, burst, lifetime, gravity,
// stopping -, the collected instances (alpha back to front), lights, the procedural sprites.

#include <g7/render/Particles.hpp>

#include <doctest/doctest.h>

#include <ostream> // doctest needs it to print std::string operands
#include <string>

using namespace g7;
using namespace g7::render;

namespace
{
std::shared_ptr<const EmitterDef> def(std::string_view toml)
{
    auto parsed = EmitterDef::parse(toml, "fx/test.toml");
    REQUIRE_MESSAGE(parsed.ok(), (parsed.ok() ? "" : parsed.error().message));
    return std::make_shared<EmitterDef>(std::move(parsed).value());
}
} // namespace

TEST_CASE("Particles: emitter definitions and their errors")
{
    const auto fire = def(R"(
version = 1
sprite = "soft"
blend = "additive"
rate = 50
lifetime = [0.4, 0.8]
speed = [0.5, 1.0]
spread = 15
gravity = -2
size = [0.3, 0.05]
color_start = [1.0, 0.6, 0.2, 1.0]
color_end = [1.0, 0.2, 0.0, 0.0]
[light]
color = [1.0, 0.6, 0.3]
range = 6
intensity = 4
flicker = 0.2
)");
    CHECK(fire->rate == doctest::Approx(50.0f));
    CHECK(fire->additive);
    CHECK(fire->light.has_value());
    CHECK(fire->light->radius == doctest::Approx(6.0f));
    CHECK(fire->colorStart.r == doctest::Approx(1.0f));
    CHECK(fire->colorStart.g < 0.6f); // sRGB in the file, linear here

    const auto fails = [](std::string_view toml, std::string_view expected)
    {
        auto r = EmitterDef::parse(toml, "fx/bad.toml");
        REQUIRE_FALSE(r.ok());
        CHECK_MESSAGE(r.error().message.find(expected) != std::string::npos, r.error().message);
    };
    fails("sprite = \"star\"\nrate = 1\n", "sprite");
    fails("rate = 0\n", "rate or burst");
    fails("rate = 1\nlifetime = [2, 1]\n", "lifetime");
    fails("rate = 1\nblend = \"multiply\"\n", "blend");
    fails("rate = 1\ncolor_start = [1, 1]\n", "color_start");
}

TEST_CASE("Particles: rate and burst, lifetime, gravity, stopping")
{
    ParticleSystem system(1);
    const auto rising = def("rate = 100\nlifetime = [1.0, 1.0]\nspeed = [1, 1]\nspread = 0\n");
    const u32 id = system.spawn(rising, Vec3(0.0f));
    for (int i = 0; i < 50; ++i)
    {
        system.update(0.01f); // half a second
    }
    CHECK(system.particleCount() == doctest::Approx(50).epsilon(0.05));
    std::vector<ParticleInstance> additive;
    std::vector<ParticleInstance> alpha;
    system.collect(Vec3(0.0f, 0.0f, 10.0f), additive, alpha);
    REQUIRE_FALSE(additive.empty());
    CHECK(alpha.empty());
    f32 highest = 0.0f;
    for (const ParticleInstance& p : additive)
    {
        highest = std::max(highest, p.position.y);
    }
    CHECK(highest == doctest::Approx(0.5f).epsilon(0.05)); // 1 m/s straight up for 0.5 s
    for (int i = 0; i < 100; ++i)
    {
        system.update(0.01f);
    }
    CHECK(system.particleCount() == doctest::Approx(100).epsilon(0.05)); // a second's worth live at once
    system.stop(id);
    CHECK(system.alive(id));
    for (int i = 0; i < 110; ++i)
    {
        system.update(0.01f);
    }
    CHECK_FALSE(system.alive(id)); // stopped and the last one died
    CHECK(system.emitterCount() == 0);

    // A burst falls under gravity; the emitter's direction turns the definition's.
    const auto burst =
        def("rate = 0\nburst = 20\nlifetime = [2, 2]\nspeed = [2, 2]\nspread = 0\ngravity = 9.81\n");
    system.spawn(burst, Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f));
    CHECK(system.particleCount() == 20);
    for (int i = 0; i < 50; ++i)
    {
        system.update(0.01f);
    }
    additive.clear();
    system.collect(Vec3(0.0f), additive, alpha);
    CHECK(additive[0].position.x == doctest::Approx(1.0f).epsilon(0.05)); // sideways, as turned
    CHECK(additive[0].position.y < -1.0f);                                // and falling
}

TEST_CASE("Particles: alpha ones back to front; lights while emitting; the sprites")
{
    ParticleSystem system(2);
    const auto smoke = def("blend = \"alpha\"\nsprite = \"smoke\"\nrate = 0\nburst = 30\nlifetime = [5, 5]\n"
                           "speed = [0, 3]\nspread = 180\n[light]\nrange = 4\n");
    const u32 id = system.spawn(smoke, Vec3(0.0f));
    system.update(0.5f);
    std::vector<ParticleInstance> additive;
    std::vector<ParticleInstance> alpha;
    const Vec3 eye(0.0f, 0.0f, 20.0f);
    system.collect(eye, additive, alpha);
    REQUIRE(alpha.size() == 30);
    for (usize i = 1; i < alpha.size(); ++i)
    {
        CHECK(glm::length(alpha[i - 1].position - eye) >= glm::length(alpha[i].position - eye));
    }
    CHECK(alpha[0].sprite == doctest::Approx(1.0f)); // smoke layer
    std::vector<PointLight> lights;
    system.lights(lights);
    CHECK(lights.size() == 1);
    system.stop(id);
    lights.clear();
    system.lights(lights);
    CHECK(lights.empty());

    for (const ParticleSprite sprite : {ParticleSprite::Soft, ParticleSprite::Smoke, ParticleSprite::Spark})
    {
        const auto image = ParticleRenderer::spriteImage(sprite, 32);
        REQUIRE(image.size() == 32u * 32u * 4u);
        const auto alphaAt = [&](u32 x, u32 y) { return image[(y * 32 + x) * 4 + 3]; };
        CHECK(alphaAt(0, 0) == 0); // the corner is empty
        if (sprite != ParticleSprite::Smoke)
        {
            CHECK(alphaAt(16, 16) > 150); // the middle is solid
        }
    }
}

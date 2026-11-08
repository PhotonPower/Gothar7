// Spell distortion (M17 part V, decisions 11-13): the effects' `distort` fields, the distorting particles
// collected, the offsets of haze, ring and swirl (render::distortOffset = particle_distort.frag), the shipped
// effects.

#include <g7/core/FileSystem.hpp>
#include <g7/render/Distortion.hpp>
#include <g7/render/Particles.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;
using namespace g7::render;

namespace
{
EmitterDef shippedFx(std::string_view name)
{
    const auto text =
        fs::readText(fs::fromUtf8(G7_ASSET_SOURCE_DIR) / "data" / "fx" / (std::string(name) + ".toml"));
    REQUIRE(text.ok());
    auto parsed = EmitterDef::parse(text.value(), name);
    REQUIRE_MESSAGE(parsed.ok(), (parsed.ok() ? "" : parsed.error().message));
    return std::move(parsed).value();
}
} // namespace

TEST_CASE("Distortion: fields of an effect, its companions; errors")
{
    auto def = EmitterDef::parse("rate = 1\ndistort = 0.8\ndistort_shape = \"ring\"\ndistort_size = 2\n"
                                 "with = [\"a\", \"b\"]\n",
                                 "fx.toml");
    REQUIRE(def.ok());
    CHECK(def.value().distort == doctest::Approx(0.8f));
    CHECK(def.value().distortShape == DistortShape::Ring);
    CHECK(def.value().distortSize == doctest::Approx(2.0f));
    CHECK(def.value().with == std::vector<std::string>{"a", "b"});
    CHECK(EmitterDef::parse("rate = 1\n", "fx.toml").value().distort == 0.0f); // not distorting
    CHECK_FALSE(EmitterDef::parse("rate = 1\ndistort_shape = \"wave\"\n", "fx.toml").ok());
    CHECK_FALSE(EmitterDef::parse("rate = 1\ndistort = 9\n", "fx.toml").ok());
    CHECK(parseDistortShape("swirl") == DistortShape::Swirl);
    CHECK_FALSE(parseDistortShape("x").has_value());
}

TEST_CASE("Distortion: haze and swirl fade over the life, rings keep their strength; sized by the factor")
{
    auto haze = std::make_shared<EmitterDef>(
        EmitterDef::parse(
            "rate = 0\nburst = 1\nlifetime = [1, 1]\nspeed = [0, 0]\nsize = [1, 1]\ndistort = 1\n"
            "distort_size = 3\n",
            "fx.toml")
            .value());
    auto ring = std::make_shared<EmitterDef>(
        EmitterDef::parse("rate = 0\nburst = 1\nlifetime = [1, 1]\nspeed = [0, 0]\ndistort = 1\n"
                          "distort_shape = \"ring\"\n",
                          "fx.toml")
            .value());
    auto plain = std::make_shared<EmitterDef>(EmitterDef::parse("rate = 0\nburst = 3\n", "fx.toml").value());
    ParticleSystem system(1);
    system.spawn(haze, Vec3(0.0f));
    system.spawn(ring, Vec3(1.0f));
    system.spawn(plain, Vec3(2.0f));
    system.update(0.5f);
    std::vector<ParticleInstance> out;
    system.collectDistortion(out);
    REQUIRE(out.size() == 2); // the plain effect does not distort
    CHECK(out[0].size == doctest::Approx(3.0f));
    CHECK(out[0].color.r == doctest::Approx(0.5f)); // half its life: half the strength
    CHECK(out[0].color.g == doctest::Approx(0.5f)); // age
    CHECK(out[0].color.b == 0.0f);                  // haze
    CHECK(out[1].color.r == doctest::Approx(1.0f)); // a ring fades by its age in the shader
    CHECK(out[1].color.b == 1.0f);
}

TEST_CASE("Distortion: offsets stay subtle and where they belong")
{
    f32 largest = 0.0f;
    for (f32 x = -1.0f; x <= 1.0f; x += 0.05f)
    {
        for (f32 y = -1.0f; y <= 1.0f; y += 0.05f)
        {
            for (f32 age = 0.0f; age <= 1.0f; age += 0.1f)
            {
                for (const DistortShape shape : {DistortShape::Haze, DistortShape::Ring, DistortShape::Swirl})
                {
                    largest = std::max(largest, glm::length(distortOffset(shape, Vec2(x, y), 1.0f, age)));
                }
            }
        }
    }
    CHECK(largest <= kDistortRing + 1e-5f); // decision 11: about 1 % of the picture at most
    CHECK(largest > 0.005f);
    // Outside the quad's circle: nothing.
    CHECK(distortOffset(DistortShape::Haze, Vec2(1.0f, 0.5f), 1.0f, 0.3f) == Vec2(0.0f));
    // A ring pushes outwards where it is now, not far inside it.
    const Vec2 onRing = distortOffset(DistortShape::Ring, Vec2(0.5f, 0.0f), 1.0f, 0.5f);
    CHECK(onRing.x > 0.004f);
    CHECK(glm::length(distortOffset(DistortShape::Ring, Vec2(0.05f, 0.0f), 1.0f, 0.5f)) < 0.0001f);
    CHECK(glm::length(distortOffset(DistortShape::Ring, Vec2(0.5f, 0.0f), 1.0f, 1.0f)) ==
          0.0f); // gone at its end
    // A swirl turns: perpendicular to the way out.
    const Vec2 turn = distortOffset(DistortShape::Swirl, Vec2(0.4f, 0.0f), 1.0f, 0.2f);
    CHECK(std::abs(turn.x) < 1e-6f);
    CHECK(turn.y > 0.0f);
    // Strength scales it; 0 is nothing.
    CHECK(distortOffset(DistortShape::Swirl, Vec2(0.4f, 0.0f), 0.0f, 0.2f) == Vec2(0.0f));
}

TEST_CASE("Distortion: the shipped effects - bolt haze, impact ring, summon, big fires, not the torch")
{
    CHECK(shippedFx("firebolt").distort > 0.0f);
    CHECK(shippedFx("firebolt").distortShape == DistortShape::Haze);
    CHECK(shippedFx("impact_fire").with == std::vector<std::string>{"impact_ring"});
    CHECK(shippedFx("impact_ring").distortShape == DistortShape::Ring);
    CHECK(shippedFx("impact_ring").colorStart.a == 0.0f); // only the distortion is seen
    CHECK(shippedFx("invest_swirl").distortShape == DistortShape::Swirl);
    CHECK(shippedFx("summon").distort > 0.0f);
    CHECK(shippedFx("fire").distort > 0.0f);   // decision 13: camp fires, hearths, the burning
    CHECK(shippedFx("torch").distort == 0.0f); // ... not torches
}

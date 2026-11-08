// Wind (M17 weather part W): the bend of a material from its glTF marks and the model's bounds, gusts within
// their range and deterministic, the weight over the height, the displacement per kind (render::windBent is
// the formula of common/wind.glsl), the particles' drift.

#include <g7/render/Particles.hpp>
#include <g7/render/Wind.hpp>

#include <doctest/doctest.h>

#include <algorithm>

using namespace g7;
using namespace g7::render;
using asset::WindKind;

namespace
{
WindFrame fullWind(f32 time = 3.0f)
{
    return WindFrame{.direction = Vec2(1.0f, 0.0f), .strength = 1.0f, .gusts = 0.0f, .time = time};
}

WindBend bendOf(WindKind kind, f32 foot = 0.0f, f32 height = 10.0f)
{
    asset::MaterialInfo material;
    material.wind = kind;
    return windBend(material, AABB{Vec3(-1.0f, foot, -1.0f), Vec3(1.0f, foot + height, 1.0f)});
}

/// The largest displacement of a model-space point over a few seconds of wind.
f32 largestOffset(const WindBend& bend, const Mat4& model, const Vec3& point, f32 strength = 1.0f)
{
    f32 largest = 0.0f;
    for (f32 t = 0.0f; t < 6.0f; t += 0.05f)
    {
        WindFrame frame = fullWind(t);
        frame.strength = strength;
        const Vec3 still = Vec3(model * Vec4(point, 1.0f));
        largest = std::max(largest, glm::length(windBent(frame, bend, model, point) - still));
    }
    return largest;
}
} // namespace

TEST_CASE("Wind: a material's bend from its marks and the whole model's height")
{
    asset::MaterialInfo leaves;
    leaves.wind = WindKind::Leaves;
    leaves.windStrength = 1.5f;
    leaves.windPivot = Vec3(0.0f, 2.93f, 0.7f);
    const WindBend bend = windBend(leaves, AABB{Vec3(-4.0f, -0.35f, -4.0f), Vec3(4.0f, 7.38f, 4.0f)});
    CHECK(bend.kind == WindKind::Leaves);
    CHECK(bend.strength == 1.5f);
    CHECK(bend.foot == doctest::Approx(-0.35f));
    CHECK(bend.height == doctest::Approx(7.73f));
    CHECK(bend.pivot == Vec3(0.0f, 2.93f, 0.7f));
    // A flat model still gets a height to divide by.
    CHECK(windBend(leaves, AABB{Vec3(0.0f), Vec3(1.0f, 0.0f, 1.0f)}).height > 0.0f);
}

TEST_CASE("Wind: gusts stay within their range, are deterministic and change over time and place")
{
    WindFrame frame{.direction = Vec2(0.8f, 0.6f), .strength = 0.5f, .gusts = 0.3f, .time = 12.0f};
    f32 low = 1.0f;
    f32 high = 0.0f;
    for (int i = 0; i < 400; ++i)
    {
        const Vec2 place(static_cast<f32>(i) * 7.3f, static_cast<f32>(i % 17) * 11.0f);
        frame.time = static_cast<f32>(i) * 0.37f;
        const f32 s = windAt(frame, place);
        CHECK(s >= 0.5f * (1.0f - 1.5f * 0.3f) - 1e-4f);
        CHECK(s <= 0.5f * (1.0f + 1.5f * 0.3f) + 1e-4f);
        CHECK(s == windAt(frame, place)); // deterministic
        low = std::min(low, s);
        high = std::max(high, s);
    }
    CHECK(high - low > 0.2f); // gusts do vary

    for (int i = 0; i < 100; ++i)
    {
        const f32 n = windNoise(Vec2(static_cast<f32>(i) * 0.31f, static_cast<f32>(i) * -0.17f));
        CHECK(n >= 0.0f);
        CHECK(n <= 1.0f);
    }
    // Without gusts the strength is the state's; storms are capped at 1.
    CHECK(windAt(WindFrame{.strength = 0.4f}, Vec2(5.0f)) == doctest::Approx(0.4f));
    CHECK(windAt(WindFrame{.strength = 1.0f, .gusts = 1.0f, .time = 2.0f}, Vec2(50.0f)) <= 1.0f);
}

TEST_CASE("Wind: weight over the height - quadratic for trees, linear for grass, clamped")
{
    const WindBend tree = bendOf(WindKind::Trunk, -0.3f, 10.0f);
    CHECK(windWeight(tree, -0.3f) == 0.0f);
    CHECK(windWeight(tree, -1.0f) == 0.0f); // below the foot
    CHECK(windWeight(tree, 4.7f) == doctest::Approx(0.25f));
    CHECK(windWeight(tree, 9.7f) == doctest::Approx(1.0f));
    CHECK(windWeight(tree, 20.0f) == 1.0f);
    const WindBend grass = bendOf(WindKind::Grass, 0.0f, 0.4f);
    CHECK(windWeight(grass, 0.2f) == doctest::Approx(0.5f));
    CHECK(windWeight(bendOf(WindKind::None), 5.0f) == 0.0f);
    CHECK(windWeight(bendOf(WindKind::Hanging), 5.0f) == 0.0f);
}

TEST_CASE("Wind: tree tops move about 4 % of the height downwind, the foot stays")
{
    const Mat4 model = glm::translate(Mat4(1.0f), Vec3(20.0f, 3.0f, -7.0f));
    const WindBend trunk = bendOf(WindKind::Trunk, 0.0f, 10.0f);
    const f32 top = largestOffset(trunk, model, Vec3(0.0f, 10.0f, 0.0f));
    CHECK(top > 0.3f);
    CHECK(top <= 0.4f + 1e-4f); // 4 % of 10 m
    CHECK(largestOffset(trunk, model, Vec3(0.0f, 0.0f, 0.0f)) == 0.0f);
    // Downwind: along the wind's direction.
    const Vec3 bent = windBent(fullWind(), trunk, model, Vec3(0.0f, 10.0f, 0.0f));
    CHECK(bent.x > 20.0f);
    CHECK(bent.z == doctest::Approx(-7.0f));
    // Leaves follow the trunk at the same height and flutter a few centimetres on top.
    const WindBend leaves = bendOf(WindKind::Leaves, 0.0f, 10.0f);
    const Vec3 leaf = windBent(fullWind(), leaves, model, Vec3(0.0f, 10.0f, 0.0f));
    CHECK(glm::length(leaf - bent) <= 0.04f);
    // Scaled models bend by their world height.
    const Mat4 big = glm::scale(model, Vec3(2.0f));
    CHECK(largestOffset(trunk, big, Vec3(0.0f, 10.0f, 0.0f)) == doctest::Approx(2.0f * top).epsilon(0.02));
}

TEST_CASE("Wind: grass bends about a quarter of its height; wind_strength scales it")
{
    const Mat4 model = glm::translate(Mat4(1.0f), Vec3(-3.0f, 0.0f, 4.0f));
    const WindBend grass = bendOf(WindKind::Grass, 0.0f, 0.4f);
    const f32 tip = largestOffset(grass, model, Vec3(0.0f, 0.4f, 0.0f));
    CHECK(tip > 0.07f);
    CHECK(tip <= 0.1f + 1e-4f);
    CHECK(largestOffset(grass, model, Vec3(0.0f, 0.2f, 0.0f)) == doctest::Approx(tip / 2.0f).epsilon(0.1));
    WindBend weak = grass;
    weak.strength = 0.5f;
    CHECK(largestOffset(weak, model, Vec3(0.0f, 0.4f, 0.0f)) == doctest::Approx(tip / 2.0f).epsilon(0.02));
}

TEST_CASE("Wind: hanging signs swing rigidly about their pivot, at most 30 degrees")
{
    const Mat4 model = glm::rotate(glm::translate(Mat4(1.0f), Vec3(5.0f, 0.0f, 5.0f)), glm::radians(90.0f),
                                   Vec3(0.0f, 1.0f, 0.0f));
    WindBend sign = bendOf(WindKind::Hanging);
    sign.pivot = Vec3(0.0f, 2.93f, 0.7f);
    const Vec3 pivotWorld = Vec3(model * Vec4(sign.pivot, 1.0f));
    const Vec3 corner(0.1f, 2.05f, 0.25f);
    const Vec3 bottom(0.0f, 2.05f, 0.7f);
    for (f32 t = 0.0f; t < 4.0f; t += 0.5f)
    {
        const WindFrame frame = fullWind(t);
        // The pivot stays; distances to it stay (rigid).
        CHECK(glm::length(windBent(frame, sign, model, sign.pivot) - pivotWorld) < 1e-4f);
        const Vec3 a = windBent(frame, sign, model, corner);
        const Vec3 b = windBent(frame, sign, model, bottom);
        CHECK(glm::length(a - b) == doctest::Approx(glm::length(corner - bottom)).epsilon(1e-4));
        // The bottom swings downwind (+X), by at most 30 degrees.
        const Vec3 down = glm::normalize(b - pivotWorld);
        CHECK(down.x > 0.0f);
        CHECK(glm::degrees(std::acos(std::clamp(-down.y, -1.0f, 1.0f))) <= 30.0f + 1e-3f);
    }
    CHECK(largestOffset(sign, model, bottom) > 0.3f); // 0.88 m below the pivot: up to 0.44 m
}

TEST_CASE("Wind: a sign on its arm hinges about the arm - only the wind across the board swings it")
{
    asset::MaterialInfo board;
    board.wind = WindKind::Hanging;
    board.windPivot = Vec3(0.0f, 2.93f, 0.7f);
    // welt's guild signs: wall plate at z = 0, the arm along +Z, the board in the YZ plane.
    const AABB bounds{Vec3(-0.11f, 2.05f, -0.01f), Vec3(0.11f, 3.01f, 1.22f)};
    const WindBend sign = windBend(board, bounds);
    CHECK(sign.hinge == 2);
    CHECK(windBend(board, AABB{Vec3(-0.3f), Vec3(0.3f)}).hinge == 0); // a lantern: free
    CHECK(windBend(asset::MaterialInfo{}, bounds).hinge == 0);        // only hanging ones hinge

    const Mat4 model = glm::translate(Mat4(1.0f), Vec3(10.0f, 0.0f, 0.0f));
    const Vec3 bottom(0.0f, 2.05f, 0.7f);
    const Vec3 still = Vec3(model * Vec4(bottom, 1.0f));
    WindFrame along = fullWind();
    along.direction = Vec2(0.0f, 1.0f); // along the arm
    CHECK(glm::length(windBent(along, sign, model, bottom) - still) < 1e-4f);
    const Vec3 across = windBent(fullWind(), sign, model, bottom); // +X: across the board
    CHECK(across.x - still.x > 0.25f);                             // downwind
    CHECK(across.z == doctest::Approx(still.z));                   // in the plane across the arm
    WindFrame back = fullWind();
    back.direction = Vec2(-1.0f, 0.0f);
    CHECK(windBent(back, sign, model, bottom).x < still.x - 0.25f);
}

TEST_CASE("Wind: still without wind, for unmarked materials and with the option off")
{
    const Mat4 model = glm::translate(Mat4(1.0f), Vec3(1.0f, 2.0f, 3.0f));
    const Vec3 top(0.0f, 10.0f, 0.0f);
    const Vec3 still = Vec3(model * Vec4(top, 1.0f));
    CHECK(windBent(fullWind(), bendOf(WindKind::None), model, top) == still);
    WindFrame calm = fullWind();
    calm.strength = 0.0f; // [render] wind = false
    for (const WindKind kind :
         {WindKind::Trunk, WindKind::Leaves, WindKind::Grass, WindKind::Cloth, WindKind::Hanging})
    {
        CHECK(windBent(calm, bendOf(kind), model, top) == still);
    }
}

TEST_CASE("Wind: packed for the shader's Wind block")
{
    const GpuWind gpu =
        packWind(WindFrame{.direction = Vec2(0.6f, 0.8f), .strength = 0.45f, .gusts = 0.3f, .time = 17.5f});
    CHECK(gpu.state == Vec4(0.6f, 0.8f, 0.45f, 0.3f));
    CHECK(gpu.time.x == 17.5f);
}

TEST_CASE("Wind: smoke drifts with the wind by its emitter's factor, magic does not")
{
    const auto parse = [](std::string_view toml)
    {
        auto parsed = EmitterDef::parse(toml, "fx/test.toml");
        REQUIRE(parsed.ok());
        return std::make_shared<EmitterDef>(std::move(parsed).value());
    };
    const auto smoke = parse("rate = 0\nburst = 10\nlifetime = [5, 5]\nspeed = [0, 0]\nwind = 1.0\n");
    const auto magic = parse("rate = 0\nburst = 10\nlifetime = [5, 5]\nspeed = [0, 0]\n");
    CHECK(smoke->wind == 1.0f);
    CHECK(magic->wind == 0.0f);
    CHECK_FALSE(EmitterDef::parse("rate = 1\nwind = -1\n", "fx/test.toml").ok());

    ParticleSystem system(3);
    system.setWind(WindFrame{.direction = Vec2(0.0f, 1.0f), .strength = 0.5f});
    system.spawn(smoke, Vec3(0.0f));
    system.spawn(magic, Vec3(0.0f));
    for (int i = 0; i < 100; ++i)
    {
        system.update(0.01f); // a second
    }
    std::vector<ParticleInstance> additive;
    std::vector<ParticleInstance> alpha;
    system.collect(Vec3(0.0f, 0.0f, 10.0f), additive, alpha);
    REQUIRE(additive.size() == 20);
    f32 drifted = 0.0f;
    f32 stayed = 0.0f;
    for (const ParticleInstance& p : additive)
    {
        (p.position.z > 0.5f ? drifted : stayed) += 1.0f;
        if (p.position.z > 0.5f)
        {
            CHECK(p.position.z ==
                  doctest::Approx(0.5f * kWindParticleSpeed).epsilon(0.02)); // 2 m in a second
            CHECK(p.position.x == doctest::Approx(0.0f));
        }
    }
    CHECK(drifted == 10.0f);
    CHECK(stayed == 10.0f);
}

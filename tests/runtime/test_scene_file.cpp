#include <g7/runtime/FrameTimes.hpp>
#include <g7/runtime/SceneFile.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using namespace g7;

namespace
{
Result<SceneFile> parse(std::string_view toml)
{
    auto config = Config::parse(toml, "scene.toml");
    REQUIRE(config.ok());
    return parseSceneFile(config.value(), "scene.toml", fs::fromUtf8("base"));
}

Vec3 origin(const Mat4& m)
{
    return Vec3(m[3]);
}
} // namespace

TEST_CASE("SceneFile: objects, prefabs, lights, viewpoints and environment")
{
    auto scene = parse(R"(
ground = { size = 80.0, color = [0.5, 0.4, 0.3] }

[environment]
sun_direction = [0, 1, 1]
fog_start = 20
fog_density = 0.01

[prefab.hut]
[[prefab.hut.part]]
mesh = "wall.glb"
[[prefab.hut.part]]
mesh = "roof.glb"
position = [0, 1, 0]

[[object]]
mesh = "tree.glb"
position = [5, 0, -2]
rotation_y = 90
scale = 2

[[object]]
prefab = "hut"
position = [10, 0, 0]
scale = 3

[[light]]
position = [1, 2, 3]
radius = 6

[[viewpoint]]
position = [0, 2, 10]
yaw = 90
pitch = -10
)");
    REQUIRE_MESSAGE(scene.ok(), (scene.ok() ? "" : scene.error().message));
    const SceneFile& s = scene.value();
    CHECK(s.groundSize == 80.0f);
    CHECK(s.groundColor == Vec3(0.5f, 0.4f, 0.3f));
    CHECK(s.environment.sunDirection == Vec3(0, 1, 1));
    CHECK(s.environment.fogStart == 20.0f);
    CHECK_FALSE(s.environment.sunColor.has_value());

    REQUIRE(s.objects.size() == 3); // the tree + two prefab parts
    CHECK(s.objects[0].mesh == fs::fromUtf8("base") / "tree.glb");
    CHECK(nearlyEqual(origin(s.objects[0].transform), Vec3(5, 0, -2)));
    // Rotated 90° about +Y and scaled 2: local +X ends up along -Z, twice as long.
    CHECK(nearlyEqual(Vec3(s.objects[0].transform * Vec4(1, 0, 0, 0)), Vec3(0, 0, -2), 1e-5f));
    // Prefab parts: object transform * part transform (part offsets scale with the object).
    CHECK(s.objects[1].mesh == fs::fromUtf8("base") / "wall.glb");
    CHECK(nearlyEqual(origin(s.objects[2].transform), Vec3(10, 3, 0)));

    REQUIRE(s.lights.size() == 1);
    CHECK(s.lights[0].position == Vec3(1, 2, 3));
    CHECK(s.lights[0].radius == 6.0f);
    CHECK(s.lights[0].intensity == SceneLight{}.intensity); // default
    REQUIRE(s.viewpoints.size() == 1);
    CHECK(s.viewpoints[0].yaw == doctest::Approx(toRadians(90.0f)));
    CHECK(s.viewpoints[0].pitch == doctest::Approx(toRadians(-10.0f)));
}

TEST_CASE("SceneFile: an empty file is an empty scene")
{
    auto scene = parse("");
    REQUIRE(scene.ok());
    CHECK(scene.value().objects.empty());
    CHECK(scene.value().groundSize == 0.0f);
}

TEST_CASE("SceneFile: errors name the file and the entry")
{
    const auto error = [](std::string_view toml)
    {
        auto scene = parse(toml);
        REQUIRE_FALSE(scene.ok());
        return scene.error().message;
    };
    CHECK(error("[[object]]\nposition = [1, 2, 3]\n") ==
          "scene.toml: 'object[0]' needs either 'mesh' or 'prefab'");
    CHECK(error("[[object]]\nmesh = \"a.glb\"\nprefab = \"x\"\n").find("either") != std::string::npos);
    CHECK(error("[[object]]\nmesh = \"a.glb\"\nposition = [1, 2]\n") ==
          "scene.toml: 'object[0].position' must be a list of 3 numbers");
    CHECK(error("[[object]]\nmesh = \"a.glb\"\nscale = 0\n") ==
          "scene.toml: 'object[0].scale' must be positive");
    CHECK(error("[[object]]\nprefab = \"castle\"\n") ==
          "scene.toml: 'object[0]' uses unknown prefab 'castle'");
    CHECK(error("[prefab.empty]\nx = 1\n").find("has no [[prefab.empty.part]]") != std::string::npos);
    CHECK(error("[[light]]\nradius = 3\n") == "scene.toml: 'light[0]' needs a 'position'");
    CHECK(error("[[light]]\nposition = [0, 0, 0]\nradius = -1\n") ==
          "scene.toml: 'light[0].radius' must be positive");
    CHECK(error("[[viewpoint]]\nposition = [0, 0, 0]\nyaw = \"left\"\n") ==
          "scene.toml: 'viewpoint[0].yaw' must be a number");
    CHECK(error("[environment]\nfog_color = 3\n") ==
          "scene.toml: 'environment.fog_color' must be a list of 3 numbers");
}

TEST_CASE("SceneFile: loading a missing file fails")
{
    CHECK_FALSE(loadSceneFile(fs::fromUtf8("does/not/exist.toml")).ok());
}

TEST_CASE("FrameTimes: average and percentiles")
{
    FrameTimes times;
    CHECK(times.summary().frames == 0);
    CHECK(times.summary().averageFps() == 0.0);
    for (int i = 1; i <= 100; ++i)
    {
        times.add(i / 1000.0); // 1..100 ms
    }
    const FrameTimeSummary s = times.summary();
    CHECK(s.frames == 100);
    CHECK(s.averageMs == doctest::Approx(50.5));
    CHECK(s.p95Ms == doctest::Approx(95.0));
    CHECK(s.p99Ms == doctest::Approx(99.0));
    CHECK(s.worstMs == doctest::Approx(100.0));
    CHECK(s.averageFps() == doctest::Approx(1000.0 / 50.5));
    CHECK(s.toString().starts_with("100 frames: 50.50 ms avg (20 fps)"));

    FrameTimes one;
    one.add(0.004);
    CHECK(one.summary().p99Ms == doctest::Approx(4.0));
    one.clear();
    CHECK(one.size() == 0);
}

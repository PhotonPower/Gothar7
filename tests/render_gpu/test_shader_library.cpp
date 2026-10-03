// ShaderLibrary on a real driver (label "gpu"): files in a temp directory, compile errors with
// file:line, hot-reload picked up by an existing pipeline.

#include "GlFixture.hpp"

#include <g7/core/FileSystem.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <ostream> // doctest needs it to print std::string_view operands
#include <string>

using namespace g7;
using namespace g7::render;
using namespace g7::render::rhi;
using g7::test::GlFixture;
using g7::test::require;

namespace
{
constexpr std::string_view kVertex = "#version 450 core\n"
                                     "void main() {\n"
                                     "    const vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
                                     "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                     "}\n";

std::string fragmentUsingColor()
{
    return "#version 450 core\n#include \"common/tint.glsl\"\nout vec4 fragColor;\nvoid main() { fragColor = "
           "kTint; }\n";
}

std::string tint(std::string_view rgba)
{
    return "const vec4 kTint = vec4(" + std::string(rgba) + ");\n";
}

class TempShaderDir
{
public:
    TempShaderDir()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() / ("g7_shader_test_" + std::to_string(stamp));
        std::filesystem::create_directories(m_path / "common");
    }
    ~TempShaderDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    const fs::Path& path() const { return m_path; }

    /// Writes a file and moves its timestamp forward, so the change is visible even on file
    /// systems with coarse timestamps.
    void write(const std::string& relative, const std::string& text)
    {
        const fs::Path file = m_path / fs::fromUtf8(relative);
        const bool existed = std::filesystem::exists(file);
        const auto previous =
            existed ? std::filesystem::last_write_time(file) : std::filesystem::file_time_type{};
        REQUIRE(fs::writeText(file, text).ok());
        if (existed)
        {
            std::filesystem::last_write_time(file, previous + std::chrono::seconds(2));
        }
    }

private:
    fs::Path m_path;
};

struct Target
{
    Texture color;
    Framebuffer framebuffer;
};

Target makeTarget(Device& device)
{
    Target target;
    target.color = require(device.createTexture({8, 8, Format::RGBA8, 1}));
    target.framebuffer = require(device.createFramebuffer({{&target.color}, nullptr}));
    return target;
}

std::array<u8, 4> drawAndSample(Device& device, const Pipeline& pipeline, const Target& target)
{
    device.bindFramebuffer(&target.framebuffer);
    device.setViewport(0, 0, 8, 8);
    device.clear(Vec4(0, 0, 0, 1), std::nullopt);
    device.bindPipeline(pipeline);
    device.draw(3);
    const auto pixels = device.readPixels(4, 4, 1, 1, &target.framebuffer);
    REQUIRE(pixels.size() == 4);
    return {pixels[0], pixels[1], pixels[2], pixels[3]};
}

Pipeline makePipeline(Device& device, const ShaderProgram* program)
{
    PipelineDesc desc;
    desc.program = program;
    desc.cull = CullMode::None;
    desc.depthTest = false;
    return require(device.createPipeline(desc));
}
} // namespace

TEST_CASE("ShaderLibrary: load, render, hot-reload into the same pipeline")
{
    GlFixture gl;
    TempShaderDir dir;
    dir.write("tri.vert", std::string(kVertex));
    dir.write("tint.frag", fragmentUsingColor());
    dir.write("common/tint.glsl", tint("1.0, 0.0, 0.0, 1.0"));

    ShaderLibrary library(*gl.device, dir.path());
    ShaderProgram* program = require(library.load("tint", {"tri.vert", "tint.frag", {}}));
    CHECK(library.find("tint") == program);
    CHECK(library.find("nope") == nullptr);
    Pipeline pipeline = makePipeline(*gl.device, program);
    Target target = makeTarget(*gl.device);
    CHECK(drawAndSample(*gl.device, pipeline, target) == std::array<u8, 4>{255, 0, 0, 255});

    CHECK(library.reloadChanged() == 0); // nothing changed

    // Edit the include: the existing pipeline renders the new colour.
    dir.write("common/tint.glsl", tint("0.0, 0.0, 1.0, 1.0"));
    CHECK(library.reloadChanged() == 1);
    CHECK(library.find("tint") == program); // same object
    CHECK(drawAndSample(*gl.device, pipeline, target) == std::array<u8, 4>{0, 0, 255, 255});

    CHECK(gl.device->debugErrorCount() == 0);

    // A broken edit keeps the previous program ...
    dir.write("common/tint.glsl", "const vec4 kTint = oops;\n");
    CHECK(library.reloadChanged() == 0);
    // The deliberate compile error may be reported several times (Mesa: twice, NVIDIA: once).
    const u32 errorsAfterBrokenEdit = gl.device->debugErrorCount();
    CHECK(drawAndSample(*gl.device, pipeline, target) == std::array<u8, 4>{0, 0, 255, 255});

    // ... and fixing it reloads again.
    dir.write("common/tint.glsl", tint("0.0, 1.0, 0.0, 1.0"));
    CHECK(library.reloadChanged() == 1);
    CHECK(drawAndSample(*gl.device, pipeline, target) == std::array<u8, 4>{0, 255, 0, 255});
    CHECK(gl.device->debugErrorCount() == errorsAfterBrokenEdit); // no errors besides the deliberate one
}

TEST_CASE("ShaderLibrary: compile errors name the include file and line")
{
    GlFixture gl;
    TempShaderDir dir;
    dir.write("tri.vert", std::string(kVertex));
    dir.write("tint.frag", fragmentUsingColor());
    dir.write("common/tint.glsl", "// tint\nconst vec4 kTint = oops;\n");

    ShaderLibrary library(*gl.device, dir.path());
    auto result = library.load("tint", {"tri.vert", "tint.frag", {}});
    REQUIRE_FALSE(result.ok());
    MESSAGE(result.error().message);
    CHECK(result.error().message.find("common/tint.glsl:2") != std::string::npos);

    auto missing = library.load("missing", {"tri.vert", "nope.frag", {}});
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().message.find("nope.frag") != std::string::npos);
}

TEST_CASE("ShaderLibrary: defines and hot-reload polling")
{
    GlFixture gl;
    TempShaderDir dir;
    dir.write("tri.vert", std::string(kVertex));
    dir.write("def.frag", "#version 450 core\nout vec4 fragColor;\n"
                          "void main() {\n#ifdef RED\n fragColor = vec4(1, 0, 0, 1);\n#else\n"
                          " fragColor = vec4(0, 1, 0, 1);\n#endif\n}\n");
    ShaderLibrary library(*gl.device, dir.path());
    ShaderProgram* red = require(library.load("red", {"tri.vert", "def.frag", {"RED"}}));
    ShaderProgram* green = require(library.load("green", {"tri.vert", "def.frag", {}}));
    Target target = makeTarget(*gl.device);
    Pipeline redPipeline = makePipeline(*gl.device, red);
    Pipeline greenPipeline = makePipeline(*gl.device, green);
    CHECK(drawAndSample(*gl.device, redPipeline, target) == std::array<u8, 4>{255, 0, 0, 255});
    CHECK(drawAndSample(*gl.device, greenPipeline, target) == std::array<u8, 4>{0, 255, 0, 255});

    // update() polls only when enabled and at most once per interval.
    library.setHotReload(true, 10.0);
    library.update(100.0); // first poll
    dir.write("def.frag",
              "#version 450 core\nout vec4 fragColor;\nvoid main() { fragColor = vec4(1, 1, 1, 1); }\n");
    library.update(105.0); // too early: no reload
    CHECK(drawAndSample(*gl.device, redPipeline, target) == std::array<u8, 4>{255, 0, 0, 255});
    library.update(111.0); // both programs depend on def.frag
    CHECK(drawAndSample(*gl.device, redPipeline, target) == std::array<u8, 4>{255, 255, 255, 255});
    CHECK(drawAndSample(*gl.device, greenPipeline, target) == std::array<u8, 4>{255, 255, 255, 255});
}

TEST_CASE("Engine shaders compile")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    auto background = library.load("background", {"background.vert", "background.frag", {}});
    CHECK_MESSAGE(background.ok(), (background.ok() ? "" : background.error().message));
}

TEST_CASE("Background shader follows the view direction")
{
    GlFixture gl;
    ShaderLibrary library(*gl.device, fs::fromUtf8(G7_SHADER_DIR));
    ShaderProgram* program = require(library.load("background", {"background.vert", "background.frag", {}}));
    Pipeline pipeline = makePipeline(*gl.device, program);
    Target target = makeTarget(*gl.device);

    const Vec3 horizon(0.0844f, 0.0395f, 0.0331f);
    const auto centreFor = [&](f32 pitchDegrees)
    {
        render::Camera camera;
        camera.aspect = 1.0f;
        camera.transform.rotation = quatFromEuler(toRadians(pitchDegrees), 0.0f, 0.0f);
        program->setUniform("uInverseViewProjection", glm::inverse(camera.viewProjection()));
        program->setUniform("uHorizonColor", horizon);
        program->setUniform("uZenithColor", Vec3(0.0052f, 0.008f, 0.017f)); // cool dusk zenith
        program->setUniform("uSunDirection", Vec3(0.0f, -1.0f, 0.0f));      // sun and moon below the horizon
        program->setUniform("uSunColor", Vec3(0.0f));
        program->setUniform("uMoonDirection", Vec3(0.0f, -1.0f, 0.0f));
        program->setUniform("uMoon", 0.0f);
        program->setUniform("uStars", 0.0f);
        program->setUniform("uCameraPosition", camera.transform.position);
        return drawAndSample(*gl.device, pipeline, target);
    };
    const auto up = centreFor(80.0f);
    const auto level = centreFor(0.0f);
    const auto down = centreFor(-80.0f);
    // Horizon is warm (red > blue), zenith cool (blue > red); below the horizon the fog colour stays
    // exactly (the level view's centre pixel lies a hair above the horizon, so compare with the
    // uniform itself; the target stores the linear value).
    CHECK(level[0] > level[2]);
    CHECK(up[2] > up[0]);
    for (usize i = 0; i < 3; ++i)
    {
        CHECK(std::abs(int(down[i]) - static_cast<int>(std::lround(horizon[static_cast<int>(i)] * 255.0f))) <=
              1);
    }
    CHECK(gl.device->debugErrorCount() == 0);
}

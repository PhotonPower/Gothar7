#include <g7/render/ShaderPreprocessor.hpp>

#include <doctest/doctest.h>

#include <map>
#include <ostream> // doctest needs it to print std::string_view operands
#include <string>
#include <vector>

using namespace g7;
using namespace g7::render;

namespace
{
/// In-memory shader files.
ShaderFileReader readerFor(std::map<std::string, std::string> files)
{
    return [files = std::move(files)](std::string_view path) -> Result<std::string>
    {
        const auto it = files.find(std::string(path));
        if (it == files.end())
        {
            return Error{"cannot open '" + std::string(path) + "'"};
        }
        return it->second;
    };
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}
} // namespace

TEST_CASE("Shader preprocessor: includes with #line directives")
{
    const auto reader = readerFor({
        {"main.frag", "#version 450 core\n#include \"common/a.glsl\"\nvoid main() {}\n"},
        {"common/a.glsl", "#include \"common/b.glsl\"\nfloat a() { return b(); }\n"},
        {"common/b.glsl", "float b() { return 1.0; }\n"},
    });
    auto result = preprocessShader("main.frag", reader);
    REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
    const auto& shader = result.value();

    CHECK(shader.files == std::vector<std::string>{"main.frag", "common/a.glsl", "common/b.glsl"});
    CHECK(shader.source == "#version 450 core\n"
                           "#line 2 0\n"
                           "#line 1 1\n"
                           "#line 1 2\n"
                           "float b() { return 1.0; }\n"
                           "#line 2 1\n"
                           "float a() { return b(); }\n"
                           "#line 3 0\n"
                           "void main() {}\n");
}

TEST_CASE("Shader preprocessor: each file is included once")
{
    const auto reader = readerFor({
        {"main.vert", "#version 450 core\n#include \"x.glsl\"\n#include \"y.glsl\"\n#include \"x.glsl\"\n"},
        {"x.glsl", "// x\n"},
        {"y.glsl", "#include \"x.glsl\"\n// y\n"},
    });
    auto result = preprocessShader("main.vert", reader);
    REQUIRE(result.ok());
    CHECK(result.value().files.size() == 3);
    const std::string& source = result.value().source;
    CHECK(source.find("// x") == source.rfind("// x")); // exactly once
}

TEST_CASE("Shader preprocessor: defines follow #version")
{
    const auto reader = readerFor({{"a.frag", "#version 450 core\nvoid main() {}\n"}});
    const std::vector<std::string> defines = {"ALPHA_TEST", "MAX_LIGHTS 8"};
    auto result = preprocessShader("a.frag", reader, defines);
    REQUIRE(result.ok());
    CHECK(result.value().source ==
          "#version 450 core\n#define ALPHA_TEST\n#define MAX_LIGHTS 8\n#line 2 0\nvoid main() {}\n");
}

TEST_CASE("Shader preprocessor: CRLF line endings")
{
    const auto reader = readerFor({{"a.frag", "#version 450 core\r\nvoid main() {}\r\n"}});
    auto result = preprocessShader("a.frag", reader);
    REQUIRE(result.ok());
    CHECK_FALSE(contains(result.value().source, "\r"));
}

TEST_CASE("Shader preprocessor: errors")
{
    SUBCASE("missing #version")
    {
        auto result = preprocessShader("a.frag", readerFor({{"a.frag", "void main() {}\n"}}));
        REQUIRE_FALSE(result.ok());
        CHECK(contains(result.error().message, "a.frag:1: #version must be the first line"));
    }
    SUBCASE("#version in an include")
    {
        auto result =
            preprocessShader("a.frag", readerFor({{"a.frag", "#version 450 core\n#include \"b.glsl\"\n"},
                                                  {"b.glsl", "#version 450\n"}}));
        REQUIRE_FALSE(result.ok());
        CHECK(contains(result.error().message, "b.glsl:1: only the main shader may declare #version"));
    }
    SUBCASE("missing include names the including line")
    {
        auto result = preprocessShader(
            "a.frag", readerFor({{"a.frag", "#version 450 core\n\n#include \"missing.glsl\"\n"}}));
        REQUIRE_FALSE(result.ok());
        CHECK(contains(result.error().message, "a.frag:3:"));
        CHECK(contains(result.error().message, "missing.glsl"));
    }
    SUBCASE("cycle")
    {
        auto result =
            preprocessShader("a.frag", readerFor({{"a.frag", "#version 450 core\n#include \"b.glsl\"\n"},
                                                  {"b.glsl", "#include \"c.glsl\"\n"},
                                                  {"c.glsl", "#include \"b.glsl\"\n"}}));
        REQUIRE_FALSE(result.ok());
        CHECK(contains(result.error().message, "c.glsl:1: include cycle with 'b.glsl'"));
    }
    SUBCASE("malformed include")
    {
        auto result =
            preprocessShader("a.frag", readerFor({{"a.frag", "#version 450 core\n#include <b.glsl>\n"}}));
        REQUIRE_FALSE(result.ok());
        CHECK(contains(result.error().message, "a.frag:2: malformed #include"));
    }
    SUBCASE("main file missing")
    {
        CHECK_FALSE(preprocessShader("nope.frag", readerFor({})).ok());
    }
}

TEST_CASE("Shader log mapping for Mesa, Intel/AMD and NVIDIA")
{
    const std::vector<std::string> files = {"main.frag", "common/fog.glsl"};
    CHECK(mapShaderLog("0:12(5): error: `x' undeclared", files) == "main.frag:12(5): error: `x' undeclared");
    CHECK(mapShaderLog("ERROR: 1:3: 'x' : undeclared identifier", files) ==
          "ERROR: common/fog.glsl:3: 'x' : undeclared identifier");
    CHECK(mapShaderLog("1(7) : error C1008: undefined variable \"x\"", files) ==
          "common/fog.glsl:7 : error C1008: undefined variable \"x\"");
    CHECK(mapShaderLog("5:2(1): error", files) == "5:2(1): error"); // unknown source index stays
    CHECK(mapShaderLog("no locations here", files) == "no locations here");
    CHECK(mapShaderLog("0:1(1): a\n1:2(2): b\n", files) == "main.frag:1(1): a\ncommon/fog.glsl:2(2): b\n");
}

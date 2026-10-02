#include <g7/runtime/SceneFile.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <format>
#include <map>

namespace g7
{
namespace
{
/// Reads an optional 3-vector; errors name the scene file and key.
Result<std::optional<Vec3>> readVec3(const Config& config, const std::string& key, std::string_view source)
{
    if (!config.contains(key))
    {
        return std::optional<Vec3>{};
    }
    const auto values = config.find<std::vector<f64>>(key);
    if (!values || values->size() != 3)
    {
        return Error{std::format("{}: '{}' must be a list of 3 numbers", source, key)};
    }
    return std::optional<Vec3>(
        Vec3(static_cast<f32>((*values)[0]), static_cast<f32>((*values)[1]), static_cast<f32>((*values)[2])));
}

Result<std::optional<f32>> readNumber(const Config& config, const std::string& key, std::string_view source)
{
    if (!config.contains(key))
    {
        return std::optional<f32>{};
    }
    const auto value = config.find<f64>(key);
    if (!value)
    {
        return Error{std::format("{}: '{}' must be a number", source, key)};
    }
    return std::optional<f32>(static_cast<f32>(*value));
}

/// Position, rotation_y and scale of the table at `prefix` as a transform.
Result<Mat4> readTransform(const Config& config, const std::string& prefix, std::string_view source)
{
    auto position = readVec3(config, prefix + ".position", source);
    auto rotation = readNumber(config, prefix + ".rotation_y", source);
    auto scale = readNumber(config, prefix + ".scale", source);
    for (const Error* error : {position ? nullptr : &position.error(), rotation ? nullptr : &rotation.error(),
                               scale ? nullptr : &scale.error()})
    {
        if (error != nullptr)
        {
            return *error;
        }
    }
    const f32 s = scale.value().value_or(1.0f);
    if (s <= 0.0f)
    {
        return Error{std::format("{}: '{}.scale' must be positive", source, prefix)};
    }
    return sceneTransform(position.value().value_or(Vec3(0.0f)), rotation.value().value_or(0.0f), s);
}

struct PrefabPart
{
    fs::Path mesh;
    Mat4 transform;
};
} // namespace

Mat4 sceneTransform(const Vec3& position, f32 rotationYDegrees, f32 scale) noexcept
{
    Mat4 m = glm::translate(Mat4(1.0f), position);
    m = glm::rotate(m, toRadians(rotationYDegrees), Vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(m, Vec3(scale));
}

Result<SceneFile> parseSceneFile(const Config& config, std::string_view source, const fs::Path& baseDirectory)
{
    SceneFile scene;

    // [ground]
    if (auto size = readNumber(config, "ground.size", source); !size)
    {
        return size.error();
    }
    else
    {
        scene.groundSize = std::max(size.value().value_or(0.0f), 0.0f);
    }
    if (auto color = readVec3(config, "ground.color", source); !color)
    {
        return color.error();
    }
    else if (color.value())
    {
        scene.groundColor = *color.value();
    }

    // [environment]
    SceneEnvironment& env = scene.environment;
    for (auto [key, target] :
         {std::pair{"sun_direction", &env.sunDirection}, std::pair{"sun_color", &env.sunColor},
          std::pair{"ambient_sky", &env.ambientSky}, std::pair{"ambient_ground", &env.ambientGround},
          std::pair{"fog_color", &env.fogColor}})
    {
        auto value = readVec3(config, std::string("environment.") + key, source);
        if (!value)
        {
            return value.error();
        }
        *target = value.value();
    }
    for (auto [key, target] :
         {std::pair{"sun_intensity", &env.sunIntensity}, std::pair{"fog_start", &env.fogStart},
          std::pair{"fog_density", &env.fogDensity}})
    {
        auto value = readNumber(config, std::string("environment.") + key, source);
        if (!value)
        {
            return value.error();
        }
        *target = value.value();
    }

    // [prefab.<name>] with [[prefab.<name>.part]]: reusable groups (e.g. a house of wall pieces).
    std::map<std::string, std::vector<PrefabPart>, std::less<>> prefabs;
    for (const std::string& name : config.keys("prefab"))
    {
        const std::string table = "prefab." + name + ".part";
        const usize parts = config.arraySize(table);
        if (parts == 0)
        {
            return Error{std::format("{}: prefab '{}' has no [[prefab.{}.part]]", source, name, name)};
        }
        std::vector<PrefabPart>& list = prefabs[name];
        for (usize i = 0; i < parts; ++i)
        {
            const std::string prefix = std::format("{}[{}]", table, i);
            const auto mesh = config.find<std::string>(prefix + ".mesh");
            if (!mesh)
            {
                return Error{std::format("{}: '{}' needs a 'mesh'", source, prefix)};
            }
            auto transform = readTransform(config, prefix, source);
            if (!transform)
            {
                return transform.error();
            }
            list.push_back({baseDirectory / fs::fromUtf8(*mesh), transform.value()});
        }
    }

    // [[object]]: a mesh or a prefab.
    for (usize i = 0, n = config.arraySize("object"); i < n; ++i)
    {
        const std::string prefix = std::format("object[{}]", i);
        auto transform = readTransform(config, prefix, source);
        if (!transform)
        {
            return transform.error();
        }
        const auto mesh = config.find<std::string>(prefix + ".mesh");
        const auto prefab = config.find<std::string>(prefix + ".prefab");
        if (mesh.has_value() == prefab.has_value())
        {
            return Error{std::format("{}: '{}' needs either 'mesh' or 'prefab'", source, prefix)};
        }
        if (mesh)
        {
            scene.objects.push_back({baseDirectory / fs::fromUtf8(*mesh), transform.value()});
            continue;
        }
        const auto found = prefabs.find(*prefab);
        if (found == prefabs.end())
        {
            return Error{std::format("{}: '{}' uses unknown prefab '{}'", source, prefix, *prefab)};
        }
        for (const PrefabPart& part : found->second)
        {
            scene.objects.push_back({part.mesh, transform.value() * part.transform});
        }
    }

    // [[light]]
    for (usize i = 0, n = config.arraySize("light"); i < n; ++i)
    {
        const std::string prefix = std::format("light[{}]", i);
        SceneLight light;
        auto position = readVec3(config, prefix + ".position", source);
        auto color = readVec3(config, prefix + ".color", source);
        auto radius = readNumber(config, prefix + ".radius", source);
        auto intensity = readNumber(config, prefix + ".intensity", source);
        if (!position || !color || !radius || !intensity)
        {
            return !position ? position.error()
                   : !color  ? color.error()
                   : !radius ? radius.error()
                             : intensity.error();
        }
        if (!position.value())
        {
            return Error{std::format("{}: '{}' needs a 'position'", source, prefix)};
        }
        light.position = *position.value();
        light.color = color.value().value_or(light.color);
        light.radius = radius.value().value_or(light.radius);
        light.intensity = intensity.value().value_or(light.intensity);
        if (light.radius <= 0.0f)
        {
            return Error{std::format("{}: '{}.radius' must be positive", source, prefix)};
        }
        scene.lights.push_back(light);
    }

    // [[viewpoint]]
    for (usize i = 0, n = config.arraySize("viewpoint"); i < n; ++i)
    {
        const std::string prefix = std::format("viewpoint[{}]", i);
        auto position = readVec3(config, prefix + ".position", source);
        auto yaw = readNumber(config, prefix + ".yaw", source);
        auto pitch = readNumber(config, prefix + ".pitch", source);
        if (!position || !yaw || !pitch)
        {
            return !position ? position.error() : !yaw ? yaw.error() : pitch.error();
        }
        if (!position.value())
        {
            return Error{std::format("{}: '{}' needs a 'position'", source, prefix)};
        }
        scene.viewpoints.push_back({*position.value(), toRadians(yaw.value().value_or(0.0f)),
                                    toRadians(pitch.value().value_or(0.0f))});
    }
    return scene;
}

Result<SceneFile> loadSceneFile(const fs::Path& path)
{
    auto config = Config::load(path);
    if (!config)
    {
        return config.error();
    }
    return parseSceneFile(config.value(), fs::toUtf8(path), path.parent_path());
}
} // namespace g7

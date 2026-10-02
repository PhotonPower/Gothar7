#pragma once

// Interim scene description for test scenes (`--scene=<file.toml>`) until the world format and
// editor of M4. Plain data; the engine loads the meshes and lights it.

#include <g7/core/Config.hpp>
#include <g7/core/FileSystem.hpp>
#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace g7::asset
{
class Vfs;
}

namespace g7
{
/// One mesh instance; prefabs are already expanded into their parts.
struct SceneObject
{
    std::string mesh; ///< VFS path, resolved against the scene file's folder
    Mat4 transform{1.0f};
};

struct SceneLight
{
    Vec3 position{0.0f};
    f32 radius = 8.0f;
    Vec3 color{1.0f, 0.62f, 0.3f};
    f32 intensity = 3.0f;
};

/// Camera position for the start and the benchmark.
struct SceneViewpoint
{
    Vec3 position{0.0f};
    f32 yaw = 0.0f;   ///< radians, 0 = looking along -Z, + = turning left
    f32 pitch = 0.0f; ///< radians, + = up
};

/// Optional overrides of the interim environment.
struct SceneEnvironment
{
    std::optional<Vec3> sunDirection;
    std::optional<Vec3> sunColor;
    std::optional<f32> sunIntensity;
    std::optional<Vec3> ambientSky;
    std::optional<Vec3> ambientGround;
    std::optional<Vec3> fogColor;
    std::optional<f32> fogStart;
    std::optional<f32> fogDensity;
};

struct SceneFile
{
    f32 groundSize = 0.0f; ///< 0 = no ground plate
    Vec3 groundColor{0.42f, 0.40f, 0.33f};
    SceneEnvironment environment;
    std::vector<SceneObject> objects;
    std::vector<SceneLight> lights;
    std::vector<SceneViewpoint> viewpoints;
};

/// Reads a scene (format: docs/05-build.md, "Testszenen") from the VFS file `scenePath`; mesh paths
/// in it are relative to that file's folder. Errors name `scenePath` and the entry.
[[nodiscard]] Result<SceneFile> parseSceneFile(const Config& config, std::string_view scenePath);
[[nodiscard]] Result<SceneFile> loadSceneFile(const asset::Vfs& vfs, std::string_view scenePath);

/// Object transform as the scene file describes it: scale, then rotation about +Y (degrees), then
/// translation.
[[nodiscard]] Mat4 sceneTransform(const Vec3& position, f32 rotationYDegrees, f32 scale) noexcept;
} // namespace g7

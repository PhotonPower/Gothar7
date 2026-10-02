#pragma once

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace g7::render
{
class Device;

/// Which files (relative to the shader root) and defines make up a program.
struct ShaderKey
{
    std::string vertexPath;
    std::string fragmentPath;
    std::vector<std::string> defines; ///< "NAME" or "NAME VALUE"
};

/// Loads GLSL programs from files (with #include, see ShaderPreprocessor.hpp) and reloads them
/// when a file they depend on changes. Programs keep their address for their whole life, so
/// pipelines created with them pick up reloads automatically. A failed reload keeps the previous
/// program and logs the error with file and line.
class ShaderLibrary
{
public:
    ShaderLibrary(Device& device, fs::Path shaderRoot);

    /// Compiles and registers a program under `name` (replacing an existing one of that name).
    /// Errors name the shader file and line.
    [[nodiscard]] Result<rhi::ShaderProgram*> load(std::string_view name, const ShaderKey& key);
    [[nodiscard]] rhi::ShaderProgram* find(std::string_view name) noexcept;

    /// Checks the modification times of all dependencies and recompiles changed programs.
    /// Returns the number of programs successfully reloaded.
    u32 reloadChanged();

    /// Hot-reload polling, driven by update() once per frame.
    void setHotReload(bool enabled, f64 pollIntervalSeconds = 0.5) noexcept;
    [[nodiscard]] bool hotReload() const noexcept { return m_hotReload; }
    /// Calls reloadChanged() at most once per poll interval (`now` in seconds, monotonic).
    void update(f64 now);

    [[nodiscard]] const fs::Path& root() const noexcept { return m_root; }

private:
    struct Dependency
    {
        fs::Path path;
        std::filesystem::file_time_type time;
    };

    struct Entry
    {
        ShaderKey key;
        std::unique_ptr<rhi::ShaderProgram> program;
        std::vector<Dependency> dependencies;
    };

    /// Preprocesses and compiles `key`; fills `dependencies` even on failure (to retry after edits).
    Result<rhi::ShaderProgram> compile(std::string_view name, const ShaderKey& key,
                                       std::vector<Dependency>& dependencies);

    Device& m_device;
    fs::Path m_root;
    std::unordered_map<std::string, Entry> m_entries;
    bool m_hotReload = false;
    f64 m_pollInterval = 0.5;
    f64 m_lastPoll = -1.0e30;
};
} // namespace g7::render

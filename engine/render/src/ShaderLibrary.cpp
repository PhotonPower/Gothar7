#include <g7/core/Log.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/render/ShaderPreprocessor.hpp>

#include <set>
#include <utility>

namespace g7::render
{
ShaderLibrary::ShaderLibrary(Device& device, fs::Path shaderRoot)
    : m_device(device), m_root(std::move(shaderRoot))
{
}

Result<rhi::ShaderProgram> ShaderLibrary::compile(std::string_view name, const ShaderKey& key,
                                                  std::vector<Dependency>& dependencies)
{
    std::set<std::string> touched;
    const ShaderFileReader reader = [&](std::string_view path) -> Result<std::string>
    {
        touched.emplace(path);
        return fs::readText(m_root / fs::fromUtf8(path));
    };

    auto vertex = preprocessShader(key.vertexPath, reader, key.defines);
    auto fragment = vertex ? preprocessShader(key.fragmentPath, reader, key.defines)
                           : Result<PreprocessedShader>(Error{});

    // Remember every file read so far, so that an edit to any of them triggers a retry.
    dependencies.clear();
    for (const std::string& path : touched)
    {
        const fs::Path full = m_root / fs::fromUtf8(path);
        auto time = fs::lastWriteTime(full);
        dependencies.push_back({full, time ? time.value() : std::filesystem::file_time_type{}});
    }

    if (!vertex)
    {
        return vertex.error();
    }
    if (!fragment)
    {
        return fragment.error();
    }

    const std::string debugName(name);
    auto program = m_device.createShaderProgram({vertex.value().source, fragment.value().source, debugName});
    if (!program)
    {
        // Map "0:12(5)" etc. back to file:line of the stage that failed.
        const std::string& message = program.error().message;
        const bool vertexStage = message.find("(vertex)") != std::string::npos;
        const bool linkError = message.find("failed to link") != std::string::npos;
        if (linkError)
        {
            return program.error();
        }
        return Error{mapShaderLog(message, vertexStage ? vertex.value().files : fragment.value().files)};
    }
    return program;
}

Result<rhi::ShaderProgram*> ShaderLibrary::load(std::string_view name, const ShaderKey& key)
{
    Entry entry;
    entry.key = key;
    auto program = compile(name, key, entry.dependencies);
    if (!program)
    {
        return program.error();
    }

    const std::string id(name);
    if (const auto it = m_entries.find(id); it != m_entries.end())
    {
        // Keep the object (pipelines point at it), replace its contents.
        *it->second.program = std::move(program).value();
        it->second.key = key;
        it->second.dependencies = std::move(entry.dependencies);
        return it->second.program.get();
    }
    entry.program = std::make_unique<rhi::ShaderProgram>(std::move(program).value());
    rhi::ShaderProgram* result = entry.program.get();
    m_entries.emplace(id, std::move(entry));
    G7_LOG_DEBUG("render", "shader '{}' loaded ({} + {})", name, key.vertexPath, key.fragmentPath);
    return result;
}

rhi::ShaderProgram* ShaderLibrary::find(std::string_view name) noexcept
{
    const auto it = m_entries.find(std::string(name));
    return it != m_entries.end() ? it->second.program.get() : nullptr;
}

u32 ShaderLibrary::reloadChanged()
{
    u32 reloaded = 0;
    for (auto& [name, entry] : m_entries)
    {
        bool changed = false;
        for (const Dependency& dependency : entry.dependencies)
        {
            auto time = fs::lastWriteTime(dependency.path);
            if (!time || time.value() != dependency.time)
            {
                changed = true;
                break;
            }
        }
        if (!changed)
        {
            continue;
        }

        auto program = compile(name, entry.key, entry.dependencies);
        if (!program)
        {
            G7_LOG_ERROR("render", "shader '{}' reload failed, keeping the previous version:\n{}", name,
                         program.error().message);
            continue;
        }
        *entry.program = std::move(program).value();
        ++reloaded;
        G7_LOG_INFO("render", "shader '{}' reloaded", name);
    }
    return reloaded;
}

void ShaderLibrary::setHotReload(bool enabled, f64 pollIntervalSeconds) noexcept
{
    m_hotReload = enabled;
    m_pollInterval = pollIntervalSeconds;
}

void ShaderLibrary::update(f64 now)
{
    if (!m_hotReload || now - m_lastPoll < m_pollInterval)
    {
        return;
    }
    m_lastPoll = now;
    reloadChanged();
}
} // namespace g7::render

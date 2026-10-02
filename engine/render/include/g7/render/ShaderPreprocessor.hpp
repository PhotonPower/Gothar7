#pragma once

#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::render
{
/// Reads a shader file by its path relative to the shader root.
using ShaderFileReader = std::function<Result<std::string>(std::string_view path)>;

struct PreprocessedShader
{
    std::string source;
    /// Every file that went into `source`; the index is the GLSL source-string number used in the
    /// emitted #line directives (0 = the main file). Also the dependency list for hot-reload.
    std::vector<std::string> files;
};

/// Resolves `#include "path"` (relative to the shader root, each file included at most once,
/// cycles are errors) and inserts `#define`s after the mandatory `#version` line of the main
/// file. Emits #line directives so compiler messages can be mapped back with mapShaderLog().
/// Defines are given as "NAME" or "NAME VALUE".
[[nodiscard]] Result<PreprocessedShader> preprocessShader(std::string_view path,
                                                          const ShaderFileReader& reader,
                                                          std::span<const std::string> defines = {});

/// Rewrites source locations in a driver's compiler log ("0:12(5)" Mesa, "0:12:" Intel/AMD,
/// "0(12)" NVIDIA; the first number is the source-string index) to "file:line".
[[nodiscard]] std::string mapShaderLog(std::string_view log, std::span<const std::string> files);
} // namespace g7::render

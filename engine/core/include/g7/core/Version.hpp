#pragma once

#include <g7/core/Types.hpp>

#include <string_view>

namespace g7
{
struct Version
{
    u32 major = 0;
    u32 minor = 3;
    u32 patch = 0;
};

/// 0.<milestone>.<patch> until the first public release (release.md, decision 5): after milestone C 0.3.x, milestone D
/// is 0.4.0. Equal to project(VERSION) in CMakeLists.txt (a core test checks it).
inline constexpr Version kEngineVersion{0, 3, 0};
inline constexpr std::string_view kEngineName = "Gothar Engine";
} // namespace g7

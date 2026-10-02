#pragma once

#include <g7/core/Types.hpp>

#include <string_view>

namespace g7
{
struct Version
{
    u32 major = 0;
    u32 minor = 1;
    u32 patch = 0;
};

inline constexpr Version kEngineVersion{0, 1, 0};
inline constexpr std::string_view kEngineName = "Gothic7 Engine";
} // namespace g7

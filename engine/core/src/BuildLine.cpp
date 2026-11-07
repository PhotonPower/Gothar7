#include <g7/core/BuildInfo.hpp>

#include <format>

namespace g7
{
std::string versionText(const BuildInfo& info)
{
    return std::format("{} ({}{})", info.version, info.gitHash, info.dirty ? "+" : "");
}

std::string buildLine(const BuildInfo& info)
{
    return std::format("Gothar {} ({}{}, {})", info.version, info.gitHash, info.dirty ? "+" : "", info.buildType);
}
} // namespace g7

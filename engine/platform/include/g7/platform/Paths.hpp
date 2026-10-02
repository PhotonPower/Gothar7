#pragma once

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>

#include <string_view>

namespace g7::platform
{
/// Writable per-user directory for saves and config, created if missing
/// (Windows: %APPDATA%\<org>\<app>, Linux: $XDG_DATA_HOME/<org>/<app>).
[[nodiscard]] Result<fs::Path> userDataDirectory(std::string_view org, std::string_view app);
} // namespace g7::platform

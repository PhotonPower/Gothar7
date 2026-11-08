#pragma once

// The system clipboard (text only): copying a view as start options (copy_position); opening a folder.

#include <g7/core/FileSystem.hpp>
#include <g7/core/Result.hpp>

#include <string_view>

namespace g7::platform
{
/// Puts `text` on the clipboard; needs the video subsystem (a window).
[[nodiscard]] Result<void> setClipboardText(std::string_view text);
/// Opens a folder in the system's file browser (the crash reports, M17 release.md part B).
[[nodiscard]] Result<void> openFolder(const fs::Path& folder);
} // namespace g7::platform

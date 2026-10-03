#pragma once

// The system clipboard (text only): copying a view as start options (copy_position).

#include <g7/core/Result.hpp>

#include <string_view>

namespace g7::platform
{
/// Puts `text` on the clipboard; needs the video subsystem (a window).
[[nodiscard]] Result<void> setClipboardText(std::string_view text);
} // namespace g7::platform

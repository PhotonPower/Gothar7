#pragma once

// Routines without a text editor (M16 part E, decision 4): the editor keeps the routines of a site in a Lua
// file of its own, game/scripts/routines/<site>_editor.lua, with nothing but `Routine { ... }` blocks in a
// fixed order. Routines of other (hand-written) files are only shown. Writing and reading that file are plain
// functions, so a round trip is tested without the Lua VM.

#include <g7/core/Result.hpp>
#include <g7/runtime/Engine.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::editor
{
/// "routines/camp_editor.lua" for the site "camp" (VFS path below "scripts/").
[[nodiscard]] std::string editorRoutineFile(std::string_view site);
/// The site of a world path: "testworld/camp.g7world" -> "camp".
[[nodiscard]] std::string siteOf(std::string_view worldPath);

/// A routine name the file may use: rtn_ + lower-case letters, digits and '_'.
[[nodiscard]] bool validRoutineName(std::string_view name) noexcept;

/// The file's text: a header saying who writes it, then the routines sorted by name, each entry on a line
/// (from, to, state, at), entries sorted by their start. The same routines give the same bytes.
[[nodiscard]] std::string writeRoutines(std::span<const RoutineInfo> routines, std::string_view site);
/// Reads what writeRoutines wrote (the file name is set to `file`). Errors name the line.
[[nodiscard]] Result<std::vector<RoutineInfo>> parseRoutines(std::string_view text, std::string_view file);

/// Sorts a routine's entries by their start (malformed times last, in their order).
void sortEntries(RoutineInfo& routine);
} // namespace g7::editor

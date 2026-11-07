#pragma once

// Helpers for the chapter tests of M15 part E (test_engine_chapter_save.cpp,
// test_engine_leonberg_chapter.cpp): dialogues through the dialogue API, saving into a slot and going on in a
// fresh engine that loads it.

#include <g7/runtime/Engine.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <ostream> // doctest needs it to print std::string operands
#include <string>
#include <string_view>
#include <vector>

namespace g7::test
{
inline script::Value chapterRun(Engine& engine, std::string_view line)
{
    auto result = engine.runConsoleLine(line);
    REQUIRE_MESSAGE(result.ok(), std::string(line), ": ", (result.ok() ? "" : result.error().message));
    return result.value();
}

inline void chapterFrames(Engine& engine, int n)
{
    for (int i = 0; i < n; ++i)
    {
        REQUIRE(engine.runFrame());
    }
}

inline bool chapterUntil(Engine& engine, const std::function<bool()>& done, int limit = 900)
{
    for (int i = 0; i < limit && !done(); ++i)
    {
        REQUIRE(engine.runFrame());
    }
    return done();
}

/// Skips the lines until the menu shows (or the dialogue ends).
inline void chapterSkipLines(Engine& engine)
{
    for (int i = 0;
         i < 60 && engine.inDialog() && !chapterRun(engine, "dialog_state().line or ''").asString().empty();
         ++i)
    {
        engine.dialogSkip();
        REQUIRE(engine.runFrame());
    }
    REQUIRE(engine.runFrame());
}

/// Chooses the entry with this text from the menu shown.
inline void chapterChoose(Engine& engine, std::string_view text)
{
    chapterSkipLines(engine);
    std::vector<std::string> options;
    const script::Value list = chapterRun(engine, "dialog_state().options");
    if (const script::Table* t = list.asTable())
    {
        for (const script::Value& v : t->array)
        {
            options.emplace_back(v.asString());
        }
    }
    const auto it = std::ranges::find(options, text);
    REQUIRE_MESSAGE(it != options.end(), "not in the menu: ", std::string(text));
    chapterRun(engine, std::format("dialog_choose({})", it - options.begin() + 1));
    chapterSkipLines(engine);
}

/// Talks to the NPC, chooses the topics in order; "Ende" unless the last topic ends the dialogue itself.
inline void chapterTalk(Engine& engine, std::string_view npc, std::initializer_list<std::string_view> path,
                        bool end = true)
{
    REQUIRE_MESSAGE(chapterRun(engine, std::format("talk('{}')", npc)).asBool(), npc);
    REQUIRE(engine.runFrame());
    for (const std::string_view step : path)
    {
        chapterChoose(engine, step);
    }
    if (end)
    {
        chapterChoose(engine, "Ende");
    }
    REQUIRE(chapterUntil(engine, [&] { return !engine.inDialog(); }, 300));
}

/// Saves into `slot` (waits until saving is allowed: on the ground, no dialogue).
inline void chapterSave(Engine& engine, std::string_view slot)
{
    REQUIRE(chapterUntil(engine, [&] { return engine.saveBlocker().empty(); }, 300));
    REQUIRE_MESSAGE(engine.saveGame(slot).ok(), slot);
}

/// A fresh engine (no world) that loads `slot`; the caller keeps it.
inline void chapterLoad(Engine& engine, std::string_view slot)
{
    REQUIRE(engine.init().ok());
    REQUIRE(engine.runFrame());
    REQUIRE_MESSAGE(engine.loadGame(slot).ok(), slot);
    REQUIRE(chapterUntil(engine, [&] { return !engine.loading() && engine.player() != nullptr; }));
    chapterFrames(engine, 5);
}
} // namespace g7::test

#pragma once

#include <string_view>

namespace g7::detail
{
[[noreturn]] void assertFailed(std::string_view expr, std::string_view msg, const char* file, int line);
} // namespace g7::detail

namespace g7
{
/// Called by a failed G7_VERIFY/G7_ASSERT before the program ends, with a one-line description: the crash
/// handler writes its report there (M17, release.md part B). Null: none. Thread-safe; called at most once.
using FatalHook = void (*)(std::string_view what);
void setFatalHook(FatalHook hook) noexcept;
} // namespace g7

// G7_ASSERT: only active in debug builds. Use for programmer errors / invariants.
// G7_VERIFY: always active. Use where continuing would corrupt state.
#define G7_VERIFY(expr, msg)                                                                                 \
    do                                                                                                       \
    {                                                                                                        \
        if (!(expr)) [[unlikely]]                                                                            \
        {                                                                                                    \
            ::g7::detail::assertFailed(#expr, msg, __FILE__, __LINE__);                                      \
        }                                                                                                    \
    } while (false)

#ifdef NDEBUG
#define G7_ASSERT(expr, msg) ((void)0)
#else
#define G7_ASSERT(expr, msg) G7_VERIFY(expr, msg)
#endif

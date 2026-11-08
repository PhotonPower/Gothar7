#include <g7/core/Assert.hpp>
#include <g7/core/Log.hpp>

#include <atomic>
#include <cstdlib>
#include <format>
#include <string>

namespace g7
{
namespace
{
std::atomic<FatalHook> g_fatalHook{nullptr};
} // namespace

void setFatalHook(FatalHook hook) noexcept
{
    g_fatalHook.store(hook);
}
} // namespace g7

namespace g7::detail
{
void assertFailed(std::string_view expr, std::string_view msg, const char* file, int line)
{
    G7_LOG_FATAL("assert", "{} ({}) at {}:{}", msg, expr, file, line);
    if (const FatalHook hook = g_fatalHook.exchange(nullptr)) // once, even if the hook asserts itself
    {
        hook(std::format("assert: {} ({}) at {}:{}", msg, expr, file, line));
    }
    std::abort();
}
} // namespace g7::detail

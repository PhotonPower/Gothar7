// A process of its own that crashes on purpose (M17, release.md part B): the crash handler's tests start it,
// so no test crashes the test runner itself. Usage: g7_crash_probe <segv|throw|verify|none> <report folder>
#include <g7/core/Assert.hpp>
#include <g7/core/Log.hpp>
#include <g7/platform/CrashHandler.hpp>

#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace
{
volatile int* g_null = nullptr; // volatile: the compiler must not see through the crash
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fputs("usage: g7_crash_probe <segv|throw|verify|none> <report folder>\n", stderr);
        return 2;
    }
    const std::string_view kind = argv[1];
    auto installed =
        g7::platform::installCrashHandler({g7::fs::fromUtf8(argv[2]), "Gothar 9.9.9 (probe12, test)", true});
    if (!installed)
    {
        std::fputs(installed.error().message.c_str(), stderr);
        return 2;
    }
    G7_LOG_INFO("probe", "about to crash: {}", kind);
    if (kind == "segv")
    {
        *g_null = 42;
    }
    else if (kind == "throw")
    {
        throw std::runtime_error("probe exception");
    }
    else if (kind == "verify")
    {
        G7_VERIFY(kind.empty(), "probe verify");
    }
    return 0; // "none": no report
}

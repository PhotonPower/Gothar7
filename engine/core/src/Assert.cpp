#include <g7/core/Assert.hpp>
#include <g7/core/Log.hpp>

#include <cstdlib>

namespace g7::detail
{
void assertFailed(std::string_view expr, std::string_view msg, const char* file, int line)
{
    G7_LOG_FATAL("assert", "{} ({}) at {}:{}", msg, expr, file, line);
    std::abort();
}
} // namespace g7::detail

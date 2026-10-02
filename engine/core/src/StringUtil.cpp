#include <g7/core/StringId.hpp>
#include <g7/core/StringUtil.hpp>

namespace g7
{
std::string toLower(std::string_view s)
{
    std::string result(s);
    for (char& c : result)
    {
        c = toLowerAscii(c);
    }
    return result;
}

std::string toUpper(std::string_view s)
{
    std::string result(s);
    for (char& c : result)
    {
        c = toUpperAscii(c);
    }
    return result;
}

usize IgnoreCaseHash::operator()(std::string_view s) const noexcept
{
    return static_cast<usize>(StringId::hashOf(s));
}
} // namespace g7

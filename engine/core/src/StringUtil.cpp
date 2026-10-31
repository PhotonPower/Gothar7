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

u32 nextCodepoint(std::string_view utf8, usize& pos) noexcept
{
    const auto byte = [&](usize i) { return static_cast<u8>(utf8[i]); };
    const u8 lead = byte(pos);
    if (lead < 0x80)
    {
        ++pos;
        return lead;
    }
    usize length = 0;
    u32 value = 0;
    u32 minimum = 0;
    if ((lead & 0xE0) == 0xC0)
    {
        length = 2;
        value = lead & 0x1Fu;
        minimum = 0x80;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        length = 3;
        value = lead & 0x0Fu;
        minimum = 0x800;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        length = 4;
        value = lead & 0x07u;
        minimum = 0x10000;
    }
    else
    {
        ++pos;
        return kReplacementCharacter;
    }
    if (pos + length > utf8.size())
    {
        ++pos;
        return kReplacementCharacter;
    }
    for (usize i = 1; i < length; ++i)
    {
        const u8 next = byte(pos + i);
        if ((next & 0xC0) != 0x80)
        {
            ++pos;
            return kReplacementCharacter;
        }
        value = (value << 6) | (next & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
    {
        ++pos;
        return kReplacementCharacter;
    }
    pos += length;
    return value;
}

void appendUtf8(std::string& out, u32 codepoint)
{
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
    {
        codepoint = kReplacementCharacter;
    }
    if (codepoint < 0x80)
    {
        out.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

usize IgnoreCaseHash::operator()(std::string_view s) const noexcept
{
    return static_cast<usize>(StringId::hashOf(s));
}
} // namespace g7

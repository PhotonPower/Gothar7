#include <g7/render/ShaderPreprocessor.hpp>

#include <algorithm>
#include <format>
#include <regex>

namespace g7::render
{
namespace
{
std::string_view trimLeft(std::string_view text) noexcept
{
    const auto first = text.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string_view() : text.substr(first);
}

/// Splits into lines without their terminators (handles \n and \r\n).
std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    usize start = 0;
    while (start < text.size())
    {
        const usize end = text.find('\n', start);
        std::string_view line =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return lines;
}

/// `#include "a/b.glsl"` -> true with path "a/b.glsl"; path empty if the directive is malformed.
bool parseInclude(std::string_view line, std::string_view& path)
{
    line = trimLeft(line);
    if (!line.starts_with("#include"))
    {
        return false;
    }
    const auto open = line.find('"');
    const auto close = open == std::string_view::npos ? std::string_view::npos : line.find('"', open + 1);
    path = (open == std::string_view::npos || close == std::string_view::npos)
               ? std::string_view()
               : line.substr(open + 1, close - open - 1);
    return true;
}

class Preprocessor
{
public:
    Preprocessor(const ShaderFileReader& reader, std::span<const std::string> defines)
        : m_reader(reader), m_defines(defines)
    {
    }

    Result<void> run(std::string_view path, std::string_view includedFrom)
    {
        const std::string file(path);
        if (std::find(m_stack.begin(), m_stack.end(), file) != m_stack.end())
        {
            return Error{std::format("{}: include cycle with '{}'", includedFrom, file)};
        }
        if (std::find(m_result.files.begin(), m_result.files.end(), file) != m_result.files.end())
        {
            return {}; // already included once
        }
        auto text = m_reader(path);
        if (!text)
        {
            return Error{includedFrom.empty() ? text.error().message
                                              : std::format("{}: {}", includedFrom, text.error().message)};
        }

        const usize index = m_result.files.size();
        m_result.files.push_back(file);
        m_stack.push_back(file);
        const bool isMain = index == 0;
        const auto lines = splitLines(text.value());

        usize lineNumber = 0;
        bool versionSeen = false;
        for (const std::string_view line : lines)
        {
            ++lineNumber;
            const std::string_view trimmed = trimLeft(line);
            if (trimmed.starts_with("#version"))
            {
                if (!isMain)
                {
                    return Error{
                        std::format("{}:{}: only the main shader may declare #version", file, lineNumber)};
                }
                if (versionSeen || lineNumber != 1)
                {
                    return Error{std::format("{}:{}: #version must be the first line", file, lineNumber)};
                }
                versionSeen = true;
                m_result.source.append(line).append("\n");
                for (const std::string& define : m_defines)
                {
                    m_result.source.append("#define ").append(define).append("\n");
                }
                m_result.source.append(std::format("#line {} {}\n", lineNumber + 1, index));
                continue;
            }
            if (isMain && !versionSeen)
            {
                return Error{std::format("{}:1: #version must be the first line", file)};
            }

            std::string_view includePath;
            if (parseInclude(line, includePath))
            {
                const std::string location = std::format("{}:{}", file, lineNumber);
                if (includePath.empty())
                {
                    return Error{location + ": malformed #include (expected #include \"path\")"};
                }
                const usize childIndex = m_result.files.size();
                const usize sizeBefore = m_result.source.size();
                m_result.source.append(std::format("#line 1 {}\n", childIndex));
                if (auto result = run(includePath, location); !result)
                {
                    return result;
                }
                if (m_result.files.size() == childIndex)
                {
                    m_result.source.resize(sizeBefore); // already included: nothing emitted
                }
                m_result.source.append(std::format("#line {} {}\n", lineNumber + 1, index));
                continue;
            }
            m_result.source.append(line).append("\n");
        }
        if (isMain && !versionSeen)
        {
            return Error{std::format("{}:1: #version must be the first line", file)};
        }
        m_stack.pop_back();
        return {};
    }

    PreprocessedShader take() { return std::move(m_result); }

private:
    const ShaderFileReader& m_reader;
    std::span<const std::string> m_defines;
    std::vector<std::string> m_stack;
    PreprocessedShader m_result;
};
} // namespace

Result<PreprocessedShader> preprocessShader(std::string_view path, const ShaderFileReader& reader,
                                            std::span<const std::string> defines)
{
    Preprocessor preprocessor(reader, defines);
    if (auto result = preprocessor.run(path, {}); !result)
    {
        return result.error();
    }
    return preprocessor.take();
}

std::string mapShaderLog(std::string_view log, std::span<const std::string> files)
{
    // "S:L(" or "S:L:" (Mesa, Intel/AMD) or "S(L)" (NVIDIA), S = source-string index.
    static const std::regex pattern(R"(\b(\d+):(\d+)(?=[(:])|\b(\d+)\((\d+)\))");
    std::string result;
    std::string text(log);
    auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
    usize last = 0;
    for (auto it = begin; it != std::sregex_iterator(); ++it)
    {
        const std::smatch& match = *it;
        const bool nvidia = match[3].matched;
        const auto source = std::stoul(match[nvidia ? 3 : 1].str());
        const std::string line = match[nvidia ? 4 : 2].str();
        result.append(text, last, static_cast<usize>(match.position(0)) - last);
        if (source < files.size())
        {
            result.append(files[source]).append(":").append(line);
        }
        else
        {
            result.append(match.str(0));
        }
        last = static_cast<usize>(match.position(0) + match.length(0));
    }
    result.append(text, last);
    return result;
}
} // namespace g7::render

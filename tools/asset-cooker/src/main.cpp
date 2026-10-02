// g7-cook: converts source assets (glTF, images, other files) into runtime formats and packs
// them into .g7pak archives (ADR 0016, docs/06-asset-pipeline.md).
#include "Cooker.hpp"

#include <g7/core/Log.hpp>

#include <cstdio>
#include <string_view>

namespace
{
void printUsage()
{
    std::puts("usage: g7-cook [--source <dir>] [--out <dir>] [--pack <file.g7pak>] [--clean]\n"
              "  --source  source assets (default: assets/source)\n"
              "  --out     output directory (default: assets/cooked)\n"
              "  --pack    write one archive <out>/<file> instead of loose files\n"
              "  --clean   delete the output directory first");
}
} // namespace

int main(int argc, char** argv)
{
    g7::cook::CookOptions options{.source = "assets/source", .out = "assets/cooked"};
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--source" && hasValue)
        {
            options.source = g7::fs::fromUtf8(argv[++i]);
        }
        else if (arg == "--out" && hasValue)
        {
            options.out = g7::fs::fromUtf8(argv[++i]);
        }
        else if (arg == "--pack" && hasValue)
        {
            options.pack = argv[++i];
        }
        else if (arg == "--clean")
        {
            options.clean = true;
        }
        else
        {
            printUsage();
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }

    auto report = g7::cook::cook(options);
    if (!report)
    {
        G7_LOG_ERROR("cook", "{}", report.error().message);
        return 1;
    }
    return report.value().errors.empty() ? 0 : 1;
}

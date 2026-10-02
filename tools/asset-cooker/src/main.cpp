// g7-cook: converts source assets (glTF, images, other files) into runtime formats and packs
// them into .g7pak archives (ADR 0016, docs/06-asset-pipeline.md).
#include "Cooker.hpp"
#include "Textures.hpp"

#include <g7/core/Log.hpp>

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{
void printUsage()
{
    std::puts("usage: g7-cook [--source <dir>] [--out <dir>] [--pack <file.g7pak>] [--level <1-22>]\n"
              "               [--textures copy|ktx2] [--uastc-level <0-4>] [--full] [--clean]\n"
              "  --source  source assets (default: assets/source)\n"
              "  --out     output directory (default: assets/cooked)\n"
              "  --pack    write one archive <out>/<file> instead of loose files (zstd per entry)\n"
              "  --level   zstd level for --pack (default 19; 1-3 for quick development cooks)\n"
              "  --textures     ktx2 (default with libktx) writes UASTC + zstd with mips, copy keeps images\n"
              "  --uastc-level  KTX2 quality vs. speed, 0-4 (default 2)\n"
              "  --full    ignore the manifest and cook everything\n"
              "  --clean   delete the output directory first");
}
} // namespace

int main(int argc, char** argv)
{
    g7::cook::CookOptions options{.source = "assets/source", .out = "assets/cooked"};
    // The renderer uploads BC7/BC5 since M3 (#37): cook textures to KTX2 unless this build lacks libktx.
    options.textures = g7::cook::hasKtx2Encoder() ? g7::cook::TextureMode::Ktx2 : g7::cook::TextureMode::Copy;
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
        else if (arg == "--level" && hasValue)
        {
            const int level = std::atoi(argv[++i]);
            if (level < 1 || level > 22)
            {
                printUsage();
                return 2;
            }
            options.level = level;
        }
        else if (arg == "--textures" && hasValue)
        {
            const std::string_view mode = argv[++i];
            if (mode != "copy" && mode != "ktx2")
            {
                printUsage();
                return 2;
            }
            options.textures = mode == "ktx2" ? g7::cook::TextureMode::Ktx2 : g7::cook::TextureMode::Copy;
        }
        else if (arg == "--uastc-level" && hasValue)
        {
            const int level = std::atoi(argv[++i]);
            if (level < 0 || level > 4)
            {
                printUsage();
                return 2;
            }
            options.uastcLevel = static_cast<g7::u32>(level);
        }
        else if (arg == "--full")
        {
            options.full = true;
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

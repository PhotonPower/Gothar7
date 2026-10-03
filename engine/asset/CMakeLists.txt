# fastgltf parses glTF (ADR 0013), stb_image decodes PNG/JPEG (ADR 0014), zstd compresses .g7pak
# entries and libktx reads KTX2 textures (ADR 0016); all private to asset.
# The nodeps preset fetches them (fastgltf downloads simdjson itself).
include(FetchContent)

find_package(fastgltf CONFIG QUIET)
if(NOT fastgltf_FOUND)
    message(STATUS "fastgltf nicht gefunden - lade fastgltf v0.9.0 per FetchContent")
    FetchContent_Declare(fastgltf
        GIT_REPOSITORY https://github.com/spnda/fastgltf.git
        GIT_TAG v0.9.0
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(fastgltf)
endif()

find_package(Stb QUIET)
if(NOT Stb_FOUND)
    message(STATUS "stb nicht gefunden - lade stb per FetchContent")
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG f75e8d1cad7d90d72ef7a4661f1b994ef78b4e31) # same revision as the vcpkg port
    FetchContent_MakeAvailable(stb)
    set(Stb_INCLUDE_DIR "${stb_SOURCE_DIR}")
endif()
# Header-only; the implementation is compiled once in src/StbImage.cpp. Tests use it too.
add_library(g7_stb INTERFACE)
target_include_directories(g7_stb SYSTEM INTERFACE "${Stb_INCLUDE_DIR}")

find_package(zstd CONFIG QUIET)
if(NOT zstd_FOUND)
    message(STATUS "zstd nicht gefunden - lade zstd v1.5.7 per FetchContent")
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
        URL_HASH SHA256=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
        SOURCE_SUBDIR build/cmake
        SYSTEM)
    FetchContent_MakeAvailable(zstd)
    add_library(zstd::libzstd ALIAS libzstd_static)
endif()

# nlohmann-json (ADR 0017) reads the assembly data of figure parts (asset.extras.gothar, M6 D2); world_format
# and tools/walk use the same target.
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND AND NOT TARGET nlohmann_json::nlohmann_json)
    message(STATUS "nlohmann-json nicht gefunden - lade v3.12.0 per FetchContent")
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        SYSTEM)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

g7_add_module(asset DEPS core PRIVATE_LIBS fastgltf::fastgltf g7_stb zstd::libzstd nlohmann_json::nlohmann_json)

# libktx comes with vcpkg only (KTX-Software is too large for FetchContent): the nodeps preset
# builds without KTX2 support and decodeKtx2() reports that (ADR 0016).
find_package(Ktx CONFIG QUIET)
if(Ktx_FOUND)
    target_link_libraries(g7_asset PRIVATE KTX::ktx)
    target_compile_definitions(g7_asset PRIVATE G7_HAS_KTX=1)
else()
    message(STATUS "libktx nicht gefunden - asset ohne KTX2-Unterstuetzung")
endif()

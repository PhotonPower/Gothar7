# fastgltf parses glTF (ADR 0013), stb_image decodes PNG/JPEG (ADR 0014), zstd compresses .g7pak
# entries (ADR 0016); all private to asset.
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
        SOURCE_SUBDIR build/cmake
        SYSTEM)
    FetchContent_MakeAvailable(zstd)
    add_library(zstd::libzstd ALIAS libzstd_static)
endif()

g7_add_module(asset DEPS core PRIVATE_LIBS fastgltf::fastgltf g7_stb zstd::libzstd)

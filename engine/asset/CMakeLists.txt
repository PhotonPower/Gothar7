# fastgltf parses glTF (ADR 0013); private to asset. The nodeps preset fetches it (it downloads
# simdjson itself).
find_package(fastgltf CONFIG QUIET)
if(NOT fastgltf_FOUND)
    message(STATUS "fastgltf nicht gefunden - lade fastgltf v0.9.0 per FetchContent")
    include(FetchContent)
    FetchContent_Declare(fastgltf
        GIT_REPOSITORY https://github.com/spnda/fastgltf.git
        GIT_TAG v0.9.0
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(fastgltf)
endif()

g7_add_module(asset DEPS core PRIVATE_LIBS fastgltf::fastgltf)

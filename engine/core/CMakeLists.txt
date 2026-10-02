# glm is a public dependency of core (ADR 0002), toml++ a private one (ADR 0010). vcpkg provides
# both; the nodeps preset falls back to fetching the header-only releases so core still builds
# without vcpkg.
include(FetchContent)

find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    message(STATUS "glm nicht gefunden - lade glm 1.0.3 per FetchContent")
    set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
    set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.3
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(glm)
endif()

find_package(tomlplusplus CONFIG QUIET)
if(NOT tomlplusplus_FOUND)
    message(STATUS "toml++ nicht gefunden - lade toml++ v3.4.0 per FetchContent")
    FetchContent_Declare(tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(tomlplusplus)
endif()
# Use toml++ header-only with TOML_EXCEPTIONS=0 (see Config.cpp). The vcpkg port ships a DLL built
# with exceptions, whose interface defines would clash, so only its headers are taken.
get_target_property(_g7_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
add_library(g7_tomlplusplus INTERFACE)
target_include_directories(g7_tomlplusplus SYSTEM INTERFACE ${_g7_toml_includes})
target_compile_definitions(g7_tomlplusplus INTERFACE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)

g7_add_module(core
    PUBLIC_LIBS glm::glm
    PRIVATE_LIBS g7_tomlplusplus)

# Must be identical in every translation unit that sees glm types.
target_compile_definitions(g7_core PUBLIC
    GLM_FORCE_CTOR_INIT
    GLM_FORCE_SILENT_WARNINGS
    GLM_ENABLE_EXPERIMENTAL)

# G7_PROFILE_* macros are active only with G7_PROFILING=ON (see Profiler.hpp).
target_compile_definitions(g7_core PUBLIC G7_PROFILING=$<BOOL:${G7_PROFILING}>)

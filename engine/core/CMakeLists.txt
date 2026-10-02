# glm is a public dependency of core (ADR 0002). vcpkg provides it; the nodeps preset
# falls back to fetching the header-only release so core still builds without vcpkg.
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    message(STATUS "glm nicht gefunden - lade glm 1.0.3 per FetchContent")
    include(FetchContent)
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

g7_add_module(core PUBLIC_LIBS glm::glm)

# Must be identical in every translation unit that sees glm types.
target_compile_definitions(g7_core PUBLIC
    GLM_FORCE_CTOR_INIT
    GLM_FORCE_SILENT_WARNINGS
    GLM_ENABLE_EXPERIMENTAL)

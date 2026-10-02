# SDL3 is a private dependency of platform (ADR 0011). vcpkg provides it; the nodeps preset
# builds a static SDL3 from source so the engine still builds without vcpkg.
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    message(STATUS "SDL3 nicht gefunden - baue SDL 3.4.16 per FetchContent")
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.16
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(SDL3)
endif()

g7_add_module(platform DEPS core PRIVATE_LIBS SDL3::SDL3)

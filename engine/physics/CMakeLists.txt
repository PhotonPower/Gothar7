# Jolt Physics (ADR 0004), private to this module: the API speaks in engine types only.
# vcpkg builds it as "joltphysics"; the nodeps preset fetches the same release.
include(FetchContent)

find_package(Jolt CONFIG QUIET)
if(NOT Jolt_FOUND)
    message(STATUS "Jolt nicht gefunden - lade JoltPhysics v5.6.0 per FetchContent")
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE) # same CRT as the engine
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(JoltPhysics
        URL https://github.com/jrouwe/JoltPhysics/archive/v5.6.0.tar.gz
        URL_HASH SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
        SOURCE_SUBDIR Build
        SYSTEM)
    FetchContent_MakeAvailable(JoltPhysics)
    if(NOT TARGET Jolt::Jolt)
        add_library(Jolt::Jolt ALIAS Jolt)
    endif()
endif()

g7_add_module(physics DEPS core PRIVATE_LIBS Jolt::Jolt)

cmake_minimum_required(VERSION 3.25)

project(Gothar
    VERSION 0.1.0
    DESCRIPTION "Gothar - eine Open-World-RPG-Engine im Stil von Gothic 1"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(G7_BUILD_TESTS  "Unit-Tests bauen (benoetigt doctest)" ON)
option(G7_BUILD_TOOLS  "Tools (Asset-Cooker, Editor) bauen" ON)
option(G7_WARNINGS_AS_ERRORS "Warnungen als Fehler behandeln" OFF)
option(G7_PROFILING   "Profiler-Zonen (G7_PROFILE_*) aktivieren" OFF)
option(G7_COVERAGE    "Testabdeckung messen (GCC/Clang, --coverage)" OFF)
option(G7_DEV_ASSETS  "Entwicklung: Repo-Ordner assets/ ins VFS mounten ([assets] dev_mounts); fuer Auslieferung OFF" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(G7Module)

if(G7_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # atomic counters: tests run zones and StringId registration on several threads
        add_compile_options(--coverage -O0 -fprofile-update=atomic)
        add_link_options(--coverage)
    else()
        message(WARNING "G7_COVERAGE wird nur mit GCC/Clang unterstuetzt - ignoriert.")
    endif()
endif()

add_subdirectory(engine)
add_subdirectory(tools/editor) # editor mode, linked into the game
add_subdirectory(tools/walk)   # autopilot (--walk), linked into the game
add_subdirectory(game)

if(G7_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(G7_BUILD_TESTS)
    find_package(doctest CONFIG QUIET)
    if(doctest_FOUND)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(WARNING "doctest nicht gefunden - Tests werden uebersprungen (vcpkg-Preset verwenden).")
    endif()
endif()

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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(G7Module)

add_subdirectory(engine)
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

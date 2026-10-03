# Lua 5.4 and sol2 (ADR 0006), private to script: the public API carries only g7::script::Value.
# vcpkg pins Lua to 5.4.7 (overrides in vcpkg.json; the baseline would bring 5.5). The nodeps preset fetches
# the same versions and builds Lua from its sources.
include(FetchContent)

find_package(Lua 5.4 QUIET)
if(LUA_FOUND)
    set(_g7_lua_include "${LUA_INCLUDE_DIR}")
    set(_g7_lua_libs ${LUA_LIBRARIES})
else()
    message(STATUS "Lua nicht gefunden - lade Lua 5.4.7 per FetchContent")
    enable_language(C) # Lua is C; the project itself is C++ only
    FetchContent_Declare(g7_lua
        URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
        URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
        SYSTEM)
    FetchContent_MakeAvailable(g7_lua)
    file(GLOB _g7_lua_sources "${g7_lua_SOURCE_DIR}/src/*.c")
    list(REMOVE_ITEM _g7_lua_sources "${g7_lua_SOURCE_DIR}/src/lua.c" "${g7_lua_SOURCE_DIR}/src/luac.c")
    add_library(g7_lua STATIC ${_g7_lua_sources})
    target_include_directories(g7_lua SYSTEM PUBLIC "${g7_lua_SOURCE_DIR}/src")
    if(UNIX)
        target_compile_definitions(g7_lua PRIVATE LUA_USE_POSIX)
        target_link_libraries(g7_lua PRIVATE m)
    endif()
    set_target_properties(g7_lua PROPERTIES FOLDER "thirdparty")
    set(_g7_lua_include "${g7_lua_SOURCE_DIR}/src")
    set(_g7_lua_libs g7_lua)
endif()

find_package(sol2 CONFIG QUIET)
if(NOT sol2_FOUND)
    message(STATUS "sol2 nicht gefunden - lade sol2 v3.5.0 per FetchContent")
    # Header-only: only the include folder is used (its CMake project would add tests and examples).
    FetchContent_Declare(g7_sol2
        URL https://github.com/ThePhD/sol2/archive/v3.5.0.tar.gz
        URL_HASH SHA256=86c0f6d2836b184a250fc2907091c076bf53c9603dd291eaebade36cc342e13c
        SOURCE_SUBDIR nonexistent
        SYSTEM)
    FetchContent_MakeAvailable(g7_sol2)
    add_library(g7_sol2 INTERFACE)
    target_include_directories(g7_sol2 SYSTEM INTERFACE "${g7_sol2_SOURCE_DIR}/include")
    add_library(sol2::sol2 ALIAS g7_sol2)
endif()

g7_add_module(script DEPS core PRIVATE_LIBS sol2::sol2 ${_g7_lua_libs})
target_include_directories(g7_script SYSTEM PRIVATE "${_g7_lua_include}")

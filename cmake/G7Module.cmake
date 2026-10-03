# g7_add_module(<name> [DEPS <g7-module>...] [PUBLIC_LIBS <lib>...] [PRIVATE_LIBS <lib>...])
#
# Legt eine Engine-Bibliothek an:
#   Target:  g7_<name>   Alias: g7::<name>
#   Headers: engine/<name>/include/g7/<name>/*.hpp  (oeffentliche API)
#   Quellen: engine/<name>/src/*.cpp                 (Implementierung)
#
# DEPS sind andere Engine-Module (nur Namen, z.B. "core"). Die erlaubte
# Abhaengigkeitsrichtung ist in docs/02-architecture.md festgelegt.
function(g7_add_module name)
    cmake_parse_arguments(ARG "" "" "DEPS;PUBLIC_LIBS;PRIVATE_LIBS" ${ARGN})

    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")

    set(_target g7_${name})
    add_library(${_target} STATIC ${_sources} ${_headers})
    add_library(g7::${name} ALIAS ${_target})

    target_include_directories(${_target}
        PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

    foreach(_dep IN LISTS ARG_DEPS)
        target_link_libraries(${_target} PUBLIC g7::${_dep})
    endforeach()
    if(ARG_PUBLIC_LIBS)
        target_link_libraries(${_target} PUBLIC ${ARG_PUBLIC_LIBS})
    endif()
    if(ARG_PRIVATE_LIBS)
        target_link_libraries(${_target} PRIVATE ${ARG_PRIVATE_LIBS})
    endif()

    target_compile_features(${_target} PUBLIC cxx_std_20)
    g7_set_warnings(${_target})
    set_target_properties(${_target} PROPERTIES FOLDER "engine")
endfunction()

# g7_assert_no_link_to(<target> <forbidden-target>...)
#
# Fails the configure step if <target> links any of the forbidden targets, directly or through
# other targets (LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES, aliases resolved). Keeps tools from
# pulling in engine layers they must not need (e.g. g7-cook must not link render or physics).
# Call it after all targets are defined.
function(g7_assert_no_link_to target)
    set(_queue ${target})
    set(_seen "")
    while(_queue)
        list(POP_FRONT _queue _current)
        string(REGEX REPLACE [=[^\$<LINK_ONLY:(.*)>$]=] [=[\1]=] _current "${_current}")
        if(NOT TARGET "${_current}")
            continue()
        endif()
        get_target_property(_aliased "${_current}" ALIASED_TARGET)
        if(_aliased)
            set(_current "${_aliased}")
        endif()
        if("${_current}" IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_current}")
        if("${_current}" IN_LIST ARGN)
            message(FATAL_ERROR "${target} must not link ${_current} (docs/02-architecture.md)")
        endif()
        foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_links "${_current}" ${_property})
            if(_links)
                list(APPEND _queue ${_links})
            endif()
        endforeach()
    endwhile()
endfunction()

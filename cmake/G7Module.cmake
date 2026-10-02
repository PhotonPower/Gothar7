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

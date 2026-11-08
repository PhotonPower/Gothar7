# Builds the release package (M17, docs/modules/release.md part C) from a clean checkout, step by step:
#   cmake -P tools/release/make_release.cmake
# 1. configure and build the preset "package" (release build without the repository's dev mounts);
# 2. the figures from parts (g7_figures, needs Python with numpy, see docs/05-build.md);
# 3. the release data: licence check, CREDITS.txt, data.g7pak (target g7_release_data);
# 4. the ZIP (CPack) in build/package/release/;
# 5. the package test: unpacks the ZIP into a temporary folder of its own and starts the game from there.
cmake_path(GET CMAKE_SCRIPT_MODE_FILE PARENT_PATH _here)
cmake_path(GET _here PARENT_PATH _tools)
cmake_path(GET _tools PARENT_PATH _root)
set(_build "${_root}/build/package")

function(g7_step)
    message(STATUS "> ${ARGN}")
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${_root}" RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "failed (${_rc}): ${ARGN}")
    endif()
endfunction()

g7_step(${CMAKE_COMMAND} --preset package)
g7_step(${CMAKE_COMMAND} --build --preset package --target gothar g7-cook g7_tests_package) # not every test
g7_step(${CMAKE_COMMAND} --build --preset package --target g7_figures)
g7_step(${CMAKE_COMMAND} --build --preset package --target g7_release_data)
g7_step(${CMAKE_CPACK_COMMAND} --config "${_build}/CPackConfig.cmake")
file(GLOB _packages "${_build}/release/Gothar-*.zip" "${_build}/release/Gothar-*.tar.gz")
list(LENGTH _packages _count)
if(NOT _count EQUAL 1)
    message(FATAL_ERROR "expected one package in ${_build}/release, found: ${_packages}")
endif()

g7_step(${CMAKE_COMMAND} -E env "G7_PACKAGE=${_packages}" ${CMAKE_CTEST_COMMAND} --test-dir "${_build}" -L package
        --output-on-failure)
message(STATUS "package: ${_packages}")

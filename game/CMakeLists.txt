add_executable(gothar src/main.cpp)
target_link_libraries(gothar PRIVATE g7::runtime g7_editor g7_walk)
g7_set_warnings(gothar)
set_target_properties(gothar PROPERTIES FOLDER "game")

# Ship the default configuration (gameDir/config/engine.toml) and the engine shaders (gameDir/shaders;
# hot-reload watches this copy) next to the executable. A separate target that runs on every build:
# a POST_BUILD step would only run when gothar relinks, so edits to a shader or engine.toml alone
# would not arrive.
add_custom_target(gothar_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:gothar>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/engine/render/shaders" "$<TARGET_FILE_DIR:gothar>/shaders"
    COMMENT "Copying default configuration and engine shaders"
    VERBATIM)
set_target_properties(gothar_data PROPERTIES FOLDER "game")
add_dependencies(gothar gothar_data)

# Figures from parts (docs/design/characters-pipeline.md §6.2): `cmake --build --preset debug --target g7_figures`.
# Optional and not part of ALL: needs Python 3 with numpy (no Blender, no DATA_ROOT); without it the target only
# says so. Interpreter: -DG7_FIGURES_PYTHON=..., else the figures track's tools/chargen/.venv, else find_package.
set(G7_FIGURES_PYTHON "" CACHE FILEPATH "Python 3 with numpy for g7_figures (empty: chargen .venv or the one found)")
set(_g7_python "${G7_FIGURES_PYTHON}")
foreach(_candidate "${PROJECT_SOURCE_DIR}/tools/chargen/.venv/Scripts/python.exe"
                   "${PROJECT_SOURCE_DIR}/tools/chargen/.venv/bin/python")
    if(NOT _g7_python AND EXISTS "${_candidate}")
        set(_g7_python "${_candidate}")
    endif()
endforeach()
if(NOT _g7_python)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        set(_g7_python "${Python3_EXECUTABLE}")
    endif()
endif()
set(_g7_numpy 1)
if(_g7_python)
    execute_process(COMMAND "${_g7_python}" -c "import numpy" RESULT_VARIABLE _g7_numpy OUTPUT_QUIET ERROR_QUIET)
endif()
if(_g7_numpy EQUAL 0)
    add_custom_target(g7_figures
        COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${PROJECT_SOURCE_DIR}/tools/chargen/src"
                "${_g7_python}" -m gothar_chargen assemble
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Assembling the figures (gothar-chargen assemble, ${_g7_python})"
        VERBATIM)
else()
    add_custom_target(g7_figures
        COMMAND ${CMAKE_COMMAND} -E echo
                "g7_figures: no Python 3 with numpy (G7_FIGURES_PYTHON, tools/chargen/.venv) - figures skipped (characters-pipeline.md 6.2)"
        VERBATIM)
endif()
set_target_properties(g7_figures PROPERTIES FOLDER "game")

# Ein Test-Executable pro Engine-Modul: g7_tests_<modul>
function(g7_add_test_suite name)
    file(GLOB _sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp")
    add_executable(g7_tests_${name} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${_sources})
    target_link_libraries(g7_tests_${name} PRIVATE g7::${name} doctest::doctest)
    g7_set_warnings(g7_tests_${name})
    set_target_properties(g7_tests_${name} PROPERTIES FOLDER "tests")
    add_test(NAME ${name} COMMAND g7_tests_${name})
endfunction()

g7_add_test_suite(core)
g7_add_test_suite(runtime)
g7_add_test_suite(platform)
g7_add_test_suite(asset)
target_compile_definitions(g7_tests_asset PRIVATE G7_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets/source")
# CI builds g7_figures first: then the C++ figure assembly must be compared with every assembled figure (M6 D2).
option(G7_REQUIRE_FIGURES "Asset tests fail when assembled figures (g7_figures) are missing" OFF)
if(G7_REQUIRE_FIGURES)
    target_compile_definitions(g7_tests_asset PRIVATE G7_REQUIRE_FIGURES=1)
endif()
# test_input_sdl.cpp injects synthetic SDL events and a virtual gamepad.
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(g7_tests_platform PRIVATE SDL3::SDL3)

# Window tests run without a display (CI, headless machines): SDL's offscreen video driver.
set_tests_properties(platform runtime PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
# test_actions.cpp validates the shipped default configuration.
target_compile_definitions(g7_tests_platform PRIVATE G7_GAME_CONFIG_DIR="${CMAKE_SOURCE_DIR}/game/config")
# test_engine_input.cpp drives the engine with synthetic SDL events.
target_link_libraries(g7_tests_runtime PRIVATE SDL3::SDL3)
target_compile_definitions(g7_tests_runtime PRIVATE G7_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets/source"
                                                   G7_GAME_CONFIG="${PROJECT_SOURCE_DIR}/game/config/engine.toml")

g7_add_test_suite(render)
g7_add_test_suite(physics)
g7_add_test_suite(gameplay)
target_compile_definitions(g7_tests_gameplay PRIVATE G7_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets/source"
                                                    G7_SCRIPT_DIR="${PROJECT_SOURCE_DIR}/game/scripts")
g7_add_test_suite(animation)
g7_add_test_suite(script)
target_compile_definitions(g7_tests_animation PRIVATE G7_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets/source")
g7_add_test_suite(world)
g7_add_test_suite(ai)
# test_day_cycle.cpp checks the shipped day-and-night curves.
target_compile_definitions(g7_tests_world PRIVATE G7_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets/source")
g7_add_test_suite(ui)
# test_debug_ui.cpp inspects ImGui's state directly.
find_package(imgui CONFIG QUIET)
target_link_libraries(g7_tests_ui PRIVATE imgui::imgui)

# Tests that need a real OpenGL 4.5+ driver. Label "gpu": CI runs them on Linux under Xvfb with
# Mesa llvmpipe and excludes them on Windows (ctest -LE gpu), whose runners have no OpenGL.
file(GLOB _gpu_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/render_gpu/*.cpp")
add_executable(g7_tests_render_gpu ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${_gpu_sources})
target_link_libraries(g7_tests_render_gpu PRIVATE g7::runtime g7_editor g7_walk doctest::doctest) # editor, autopilot end to end
g7_set_warnings(g7_tests_render_gpu)
set_target_properties(g7_tests_render_gpu PROPERTIES FOLDER "tests")
# Several ctest entries over one executable, so each stays within its timeout in the coverage job (software GL plus
# instrumentation is 10-15 times slower): the render tests, and the engine-level ones (whole engine with a world)
# in groups of similar run time. Every test case runs in exactly one entry (the last group takes what the others
# leave); a new engine-level test needs no entry of its own.
set(_g7_gpu_engine_cases "*GPU:*,Test world*,Test scene*,Engine*")
set(_g7_gpu_group_water "Player GPU: swims*,Player GPU: dives*,Player GPU: walks into the pond*")
set(_g7_gpu_group_player "Player GPU*")
set(_g7_gpu_group_creatures "Creatures GPU*,Autopilot GPU*")
set(_g7_gpu_group_figures "Player figure GPU*,Scripts GPU*,Terrain GPU*,Start view GPU*")
add_test(NAME render_gpu COMMAND g7_tests_render_gpu "--test-case-exclude=${_g7_gpu_engine_cases}")
add_test(NAME render_gpu_water COMMAND g7_tests_render_gpu "--test-case=${_g7_gpu_group_water}")
add_test(NAME render_gpu_player COMMAND g7_tests_render_gpu "--test-case=${_g7_gpu_group_player}"
         "--test-case-exclude=${_g7_gpu_group_water}")
add_test(NAME render_gpu_creatures COMMAND g7_tests_render_gpu "--test-case=${_g7_gpu_group_creatures}")
add_test(NAME render_gpu_figures COMMAND g7_tests_render_gpu "--test-case=${_g7_gpu_group_figures}")
add_test(NAME render_gpu_engine COMMAND g7_tests_render_gpu "--test-case=${_g7_gpu_engine_cases}"
         "--test-case-exclude=${_g7_gpu_group_player},${_g7_gpu_group_creatures},${_g7_gpu_group_figures}")
set(_g7_gpu_tests render_gpu render_gpu_water render_gpu_player render_gpu_creatures render_gpu_figures
    render_gpu_engine)
set_tests_properties(${_g7_gpu_tests} PROPERTIES LABELS gpu)
target_compile_definitions(g7_tests_render_gpu PRIVATE G7_SHADER_DIR="${PROJECT_SOURCE_DIR}/engine/render/shaders"
    G7_TESTSCENE="${PROJECT_SOURCE_DIR}/assets/source/testscene/scene.toml")
# test_image.cpp encodes PNG/JPEG with stb_image_write.
target_link_libraries(g7_tests_asset PRIVATE g7_stb)
target_link_libraries(g7_tests_render_gpu PRIVATE g7_stb) # test_material.cpp encodes PNGs

# g7-cook (tools/asset-cooker): cooker logic as a library; tests encode PNGs with stb_image_write.
if(TARGET g7_cook_lib)
    file(GLOB _cook_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cook/*.cpp")
    add_executable(g7_tests_cook ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${_cook_sources})
    target_link_libraries(g7_tests_cook PRIVATE g7_cook_lib doctest::doctest g7_stb)
    g7_set_warnings(g7_tests_cook)
    set_target_properties(g7_tests_cook PROPERTIES FOLDER "tests")
    add_test(NAME cook COMMAND g7_tests_cook)
    # test_ktx2.cpp checks transcoded pixels with libktx directly (vcpkg builds only).
    find_package(Ktx CONFIG QUIET)
    if(Ktx_FOUND)
        target_link_libraries(g7_tests_cook PRIVATE KTX::ktx)
        target_compile_definitions(g7_tests_cook PRIVATE G7_HAS_KTX=1)
    endif()
endif()

# A hanging test turns the run red instead of blocking it for hours (CI and local runs). The GPU
# suite gets more time: under Mesa llvmpipe (CI) it renders in software.
set(G7_TEST_TIMEOUT 300 CACHE STRING "Seconds per test suite before ctest stops it")
get_property(_g7_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_g7_tests} PROPERTIES TIMEOUT ${G7_TEST_TIMEOUT})
set_tests_properties(${_g7_gpu_tests} PROPERTIES TIMEOUT 900)

# Editor mode (tools/editor): gizmo maths and world operations on the CPU.
file(GLOB _editor_test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp")
add_executable(g7_tests_editor ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${_editor_test_sources})
target_link_libraries(g7_tests_editor PRIVATE g7_editor doctest::doctest)
g7_set_warnings(g7_tests_editor)
set_target_properties(g7_tests_editor PROPERTIES FOLDER "tests")
add_test(NAME editor COMMAND g7_tests_editor)
set_tests_properties(editor PROPERTIES TIMEOUT ${G7_TEST_TIMEOUT})

# Autopilot (tools/walk): route files. The run itself is tested end to end in render_gpu (test_walk.cpp).
file(GLOB _walk_test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/walk/*.cpp")
add_executable(g7_tests_walk ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${_walk_test_sources})
target_link_libraries(g7_tests_walk PRIVATE g7_walk doctest::doctest)
g7_set_warnings(g7_tests_walk)
set_target_properties(g7_tests_walk PROPERTIES FOLDER "tests")
add_test(NAME walk COMMAND g7_tests_walk)
set_tests_properties(walk PROPERTIES TIMEOUT ${G7_TEST_TIMEOUT})

# Game command line: --help and unknown options must exit at once, without a window or the engine.
if(TARGET gothar)
    foreach(_case IN ITEMS "help|--help|0|--world=" "unknown|--bogus|nonzero|unknown argument '--bogus'")
        string(REPLACE "|" ";" _parts "${_case}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _arg)
        list(GET _parts 2 _rc)
        list(GET _parts 3 _expect)
        add_test(NAME game.cli.${_name}
                 COMMAND ${CMAKE_COMMAND} -DGOTHAR=$<TARGET_FILE:gothar> -DARGS=${_arg} -DEXPECT_RC=${_rc}
                         "-DEXPECT=${_expect}" -P "${CMAKE_CURRENT_SOURCE_DIR}/game/check_cli.cmake")
        set_tests_properties(game.cli.${_name} PROPERTIES TIMEOUT 60)
    endforeach()
    # docs/script-api.md is generated from the engine's script bindings (M7): it must be up to date.
    add_test(NAME game.script_api
             COMMAND ${CMAKE_COMMAND} -DGOTHAR=$<TARGET_FILE:gothar> "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/script-api.md"
                     "-DDOC=${PROJECT_SOURCE_DIR}/docs/script-api.md" -P "${CMAKE_CURRENT_SOURCE_DIR}/game/check_script_api.cmake")
    set_tests_properties(game.script_api PROPERTIES TIMEOUT 120)
endif()

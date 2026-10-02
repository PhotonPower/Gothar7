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
# test_input_sdl.cpp injects synthetic SDL events and a virtual gamepad.
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(g7_tests_platform PRIVATE SDL3::SDL3)

# Window tests run without a display (CI, headless machines): SDL's offscreen video driver.
set_tests_properties(platform runtime PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
# test_actions.cpp validates the shipped default configuration.
target_compile_definitions(g7_tests_platform PRIVATE G7_GAME_CONFIG_DIR="${CMAKE_SOURCE_DIR}/game/config")
# test_engine_input.cpp drives the engine with synthetic SDL events.
target_link_libraries(g7_tests_runtime PRIVATE SDL3::SDL3)

g7_add_test_suite(render)

# Tests that need a real OpenGL 4.5+ driver. Label "gpu": CI runs them on Linux under Xvfb with
# Mesa llvmpipe and excludes them on Windows (ctest -LE gpu), whose runners have no OpenGL.
file(GLOB _gpu_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/render_gpu/*.cpp")
add_executable(g7_tests_render_gpu ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${_gpu_sources})
target_link_libraries(g7_tests_render_gpu PRIVATE g7::runtime doctest::doctest)
g7_set_warnings(g7_tests_render_gpu)
set_target_properties(g7_tests_render_gpu PROPERTIES FOLDER "tests")
add_test(NAME render_gpu COMMAND g7_tests_render_gpu)
set_tests_properties(render_gpu PROPERTIES LABELS gpu)
target_compile_definitions(g7_tests_render_gpu PRIVATE G7_SHADER_DIR="${PROJECT_SOURCE_DIR}/engine/render/shaders")

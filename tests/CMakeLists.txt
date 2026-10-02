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

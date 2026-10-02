# Cooker logic as a library so tests can call it; g7-cook is a thin command line around it.
add_library(g7_cook_lib STATIC src/Cooker.cpp)
target_include_directories(g7_cook_lib PUBLIC src)
target_link_libraries(g7_cook_lib PUBLIC g7::core g7::asset)
g7_set_warnings(g7_cook_lib)
set_target_properties(g7_cook_lib PROPERTIES FOLDER "tools")

add_executable(g7-cook src/main.cpp)
target_link_libraries(g7-cook PRIVATE g7_cook_lib)
g7_set_warnings(g7-cook)
set_target_properties(g7-cook PROPERTIES FOLDER "tools")

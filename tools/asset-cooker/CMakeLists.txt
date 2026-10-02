add_executable(g7-cook src/main.cpp)
target_link_libraries(g7-cook PRIVATE g7::core)
g7_set_warnings(g7-cook)
set_target_properties(g7-cook PROPERTIES FOLDER "tools")

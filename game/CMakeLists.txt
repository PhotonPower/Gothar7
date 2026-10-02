add_executable(gothar src/main.cpp)
target_link_libraries(gothar PRIVATE g7::runtime)
g7_set_warnings(gothar)
set_target_properties(gothar PROPERTIES FOLDER "game")

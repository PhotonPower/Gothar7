add_executable(gothic7 src/main.cpp)
target_link_libraries(gothic7 PRIVATE g7::runtime)
g7_set_warnings(gothic7)
set_target_properties(gothic7 PROPERTIES FOLDER "game")

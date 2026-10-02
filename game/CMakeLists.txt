add_executable(gothar src/main.cpp)
target_link_libraries(gothar PRIVATE g7::runtime)
g7_set_warnings(gothar)
set_target_properties(gothar PROPERTIES FOLDER "game")

# Ship the default configuration next to the executable (gameDir/config/engine.toml).
add_custom_command(TARGET gothar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:gothar>/config"
    COMMENT "Copying default configuration")

# Ship the engine shaders next to the executable (gameDir/shaders); hot-reload watches this copy.
add_custom_command(TARGET gothar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/engine/render/shaders" "$<TARGET_FILE_DIR:gothar>/shaders"
    COMMENT "Copying engine shaders")

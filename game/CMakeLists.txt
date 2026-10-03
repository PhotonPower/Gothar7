add_executable(gothar src/main.cpp)
target_link_libraries(gothar PRIVATE g7::runtime g7_editor)
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

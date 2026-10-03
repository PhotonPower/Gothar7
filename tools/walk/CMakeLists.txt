# Autopilot of the game (gothar --walk, docs/modules/tools.md): runs the player along a route and logs
# what happens (W3 walkthrough of the Leonberg world). Like the editor a tool the engine only knows as
# EngineTool. nlohmann-json (ADR 0017) reads routes and writes the log.
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
file(GLOB _walk_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(g7_walk STATIC ${_walk_sources})
target_include_directories(g7_walk PUBLIC include)
target_link_libraries(g7_walk PUBLIC g7::runtime g7::world PRIVATE nlohmann_json::nlohmann_json)
g7_set_warnings(g7_walk)
set_target_properties(g7_walk PROPERTIES FOLDER "tools")

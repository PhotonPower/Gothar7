# Editor mode of the engine (docs/modules/tools.md, M4 foundation): picking, own gizmos (ADR 0015
# addendum), operations on the world and saving. Part of the game executable (gothar --editor); the
# engine only knows the EngineTool interface, not the editor.
file(GLOB _editor_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(g7_editor STATIC ${_editor_sources})
target_include_directories(g7_editor PUBLIC include)
target_link_libraries(g7_editor PUBLIC g7::runtime g7::world g7::ui g7::render)
g7_set_warnings(g7_editor)
set_target_properties(g7_editor PROPERTIES FOLDER "tools")
# nlohmann-json (ADR 0017) reads the glTF extras of Blender's VOB_ nodes (dungeon D7); the target comes from
# engine/asset (found or fetched there).
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
target_link_libraries(g7_editor PRIVATE nlohmann_json::nlohmann_json)

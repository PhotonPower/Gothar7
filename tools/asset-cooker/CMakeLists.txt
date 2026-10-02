# Cooker logic as a library so tests can call it; g7-cook is a thin command line around it.
add_library(g7_cook_lib STATIC src/Cooker.cpp src/Manifest.cpp src/Textures.cpp)
target_include_directories(g7_cook_lib PUBLIC src)
target_link_libraries(g7_cook_lib PUBLIC g7::core g7::asset PRIVATE g7::world) # world: terrain blocks name texture usage
g7_set_warnings(g7_cook_lib)
set_target_properties(g7_cook_lib PROPERTIES FOLDER "tools")
# KTX2 encoding needs libktx (vcpkg only, ADR 0016); without it --textures ktx2 is refused.
find_package(Ktx CONFIG QUIET)
if(Ktx_FOUND)
    target_link_libraries(g7_cook_lib PRIVATE KTX::ktx)
    target_compile_definitions(g7_cook_lib PRIVATE G7_HAS_KTX=1)
endif()

add_executable(g7-cook src/main.cpp)
target_link_libraries(g7-cook PRIVATE g7_cook_lib)
g7_set_warnings(g7-cook)
set_target_properties(g7-cook PROPERTIES FOLDER "tools")

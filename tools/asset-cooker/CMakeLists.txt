# Cooker logic as a library so tests can call it; g7-cook is a thin command line around it.
add_library(g7_cook_lib STATIC src/Cooker.cpp src/Fonts.cpp src/Manifest.cpp src/Textures.cpp)
target_include_directories(g7_cook_lib PUBLIC src)
# world_format only: terrain blocks name texture usage. The cooker must not link render or physics
# (checked below, g7_assert_no_link_to).
target_link_libraries(g7_cook_lib PUBLIC g7::core g7::asset PRIVATE g7::world_format g7_stb)
g7_set_warnings(g7_cook_lib)
set_target_properties(g7_cook_lib PROPERTIES FOLDER "tools")
# KTX2 encoding needs libktx (vcpkg only, ADR 0016); without it --textures ktx2 is refused.
find_package(Ktx CONFIG QUIET)
if(Ktx_FOUND)
    target_link_libraries(g7_cook_lib PRIVATE KTX::ktx)
    target_compile_definitions(g7_cook_lib PRIVATE G7_HAS_KTX=1)
endif()
# Distance-field fonts need msdfgen, core only (vcpkg, ADR 0020); without it --font is refused.
find_package(msdfgen CONFIG QUIET)
if(TARGET msdfgen::msdfgen-core)
    target_link_libraries(g7_cook_lib PRIVATE msdfgen::msdfgen-core)
    target_compile_definitions(g7_cook_lib PUBLIC G7_HAS_MSDFGEN=1)
else()
    target_compile_definitions(g7_cook_lib PUBLIC G7_HAS_MSDFGEN=0)
endif()

add_executable(g7-cook src/main.cpp)
target_link_libraries(g7-cook PRIVATE g7_cook_lib)
g7_set_warnings(g7-cook)
set_target_properties(g7-cook PROPERTIES FOLDER "tools")

# The cooker reads worlds through world_format only (docs/02-architecture.md).
g7_assert_no_link_to(g7-cook g7_render g7_physics g7_world g7_platform)

# Distance-field fonts of the game UI (ADR 0020): every assets/source/fonts/<name>.font.toml becomes <name>.g7font
# next to it (git-ignored), so development builds read it from the source mount and g7-cook packs it like any
# file. Part of ALL; regenerated when the description, a font file or g7-cook changes.
if(TARGET msdfgen::msdfgen-core)
    set(_g7_font_dir "${PROJECT_SOURCE_DIR}/assets/source/fonts")
    file(GLOB _g7_font_specs CONFIGURE_DEPENDS "${_g7_font_dir}/*.font.toml")
    file(GLOB _g7_font_files CONFIGURE_DEPENDS "${_g7_font_dir}/*.ttf" "${_g7_font_dir}/*.otf")
    set(_g7_font_outputs "")
    foreach(_spec IN LISTS _g7_font_specs)
        get_filename_component(_file "${_spec}" NAME)
        string(REGEX REPLACE "\.font\.toml$" "" _name "${_file}")
        set(_out "${_g7_font_dir}/${_name}.g7font")
        add_custom_command(OUTPUT "${_out}"
            COMMAND g7-cook --font "${_spec}" "${_out}"
            DEPENDS "${_spec}" ${_g7_font_files} g7-cook
            COMMENT "Generating font ${_name}.g7font"
            VERBATIM)
        list(APPEND _g7_font_outputs "${_out}")
    endforeach()
    add_custom_target(g7_fonts ALL DEPENDS ${_g7_font_outputs})
    set_target_properties(g7_fonts PROPERTIES FOLDER "tools")
    if(TARGET gothar)
        add_dependencies(gothar g7_fonts)
    endif()
endif()

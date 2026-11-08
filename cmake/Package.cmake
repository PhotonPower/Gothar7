# The release package (M17, docs/modules/release.md part C): install rules and CPack (ZIP). The data
# (data.g7pak, CREDITS.txt) comes from the target g7_release_data; tools/release/make_release.cmake runs all
# steps in order. Only files that may be shipped go in: the game, its configuration, shaders and scripts, the
# cooked assets, the credits and the licence texts.
set(G7_RELEASE_DIR "${CMAKE_BINARY_DIR}/release")

if(TARGET gothar AND TARGET g7-cook)
    add_custom_target(g7_release_data
        COMMAND ${CMAKE_COMMAND}
                -DGOTHAR=$<TARGET_FILE:gothar>
                -DCOOK=$<TARGET_FILE:g7-cook>
                -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                -DOUT=${G7_RELEASE_DIR}
                -DVERSION=${PROJECT_VERSION}
                -P ${PROJECT_SOURCE_DIR}/tools/release/release_data.cmake
        DEPENDS gothar g7-cook
        COMMENT "Release data: licence check, CREDITS.txt, data.g7pak"
        VERBATIM)
    set_target_properties(g7_release_data PROPERTIES FOLDER "tools")
endif()

if(TARGET gothar)
    install(TARGETS gothar RUNTIME DESTINATION .)
    if(WIN32)
        install(FILES $<TARGET_RUNTIME_DLLS:gothar> DESTINATION .) # vcpkg's DLLs (SDL3 ...)
    endif()
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/game/config/" DESTINATION config)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/engine/render/shaders/" DESTINATION shaders)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/game/scripts/" DESTINATION scripts)
    install(FILES "${G7_RELEASE_DIR}/data.g7pak" DESTINATION data OPTIONAL)
    install(FILES "${G7_RELEASE_DIR}/CREDITS.txt" DESTINATION . OPTIONAL)
    install(FILES "${PROJECT_SOURCE_DIR}/tools/release/README.txt"
                  "${PROJECT_SOURCE_DIR}/tools/release/THIRD_PARTY_NOTICES.txt"
            DESTINATION .)
    install(FILES "${PROJECT_SOURCE_DIR}/assets/LICENSES.md" DESTINATION LICENSES RENAME ASSETS.md)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION LICENSES RENAME GOTHAR.txt)
    install(FILES "${PROJECT_SOURCE_DIR}/assets/source/fonts/OFL.txt" DESTINATION LICENSES)
    # The licence texts of the libraries the game links (vcpkg puts them into share/<port>/copyright).
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        foreach(_port IN ITEMS sdl3 entt fastgltf simdjson glad glm imgui joltphysics lua sol2 miniaudio
                               nlohmann-json tomlplusplus stb zstd ktx)
            set(_copyright "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${_port}/copyright")
            if(EXISTS "${_copyright}")
                install(FILES "${_copyright}" DESTINATION LICENSES RENAME "${_port}.txt")
            endif()
        endforeach()
    endif()
endif()

# ZIP named after version, commit and system (release.md decisions 5 and 7).
find_package(Git QUIET)
set(_g7_hash "unknown")
if(GIT_EXECUTABLE)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                    OUTPUT_VARIABLE _g7_hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(WIN32)
    set(_g7_system "windows-x64")
else()
    set(_g7_system "linux-x64")
endif()
set(CPACK_GENERATOR "ZIP")
if(NOT WIN32)
    set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_PACKAGE_NAME "Gothar")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Gothar-${PROJECT_VERSION}-${_g7_hash}-${_g7_system}")
set(CPACK_PACKAGE_DIRECTORY "${G7_RELEASE_DIR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)

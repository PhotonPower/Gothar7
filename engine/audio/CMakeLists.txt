# miniaudio mixes and plays the sound (ADR 0007), stb_vorbis decodes OGG Vorbis; both private to audio.
# The nodeps preset fetches them.
include(FetchContent)

find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h)
if(NOT MINIAUDIO_INCLUDE_DIR)
    message(STATUS "miniaudio nicht gefunden - lade miniaudio 0.11.25 per FetchContent")
    FetchContent_Declare(miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.25
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(miniaudio)
    if(NOT miniaudio_POPULATED)
        FetchContent_Populate(miniaudio)
    endif()
    set(MINIAUDIO_INCLUDE_DIR "${miniaudio_SOURCE_DIR}")
endif()
add_library(g7_miniaudio INTERFACE)
target_include_directories(g7_miniaudio SYSTEM INTERFACE "${MINIAUDIO_INCLUDE_DIR}")
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(g7_miniaudio INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# g7_stb (stb_vorbis.c) comes from the asset module.
g7_add_module(audio DEPS core asset PRIVATE_LIBS g7_miniaudio g7_stb)

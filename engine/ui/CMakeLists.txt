# Dear ImGui for debug and editor UI (ADR 0015), private to ui with our own backends (input from
# platform::Input, rendering through the RHI). The nodeps preset fetches it.
include(FetchContent)

find_package(imgui CONFIG QUIET)
if(NOT imgui_FOUND)
    message(STATUS "imgui nicht gefunden - lade Dear ImGui v1.92.9b per FetchContent")
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9b
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui)
    add_library(g7_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp)
    target_include_directories(g7_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR})
    target_compile_features(g7_imgui PUBLIC cxx_std_20)
    add_library(imgui::imgui ALIAS g7_imgui)
endif()

g7_add_module(ui DEPS core platform render PRIVATE_LIBS imgui::imgui)

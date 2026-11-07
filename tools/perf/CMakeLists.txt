# Perf scenarios of the game (gothar --perf=<name>, docs/modules/perf.md part B): fixed views in Leonberg,
# frame times, zones, GPU passes and stats as JSON. Like the autopilot a tool the engine only knows as
# EngineTool. nlohmann-json (ADR 0017) writes the report.
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
file(GLOB _perf_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(g7_perf STATIC ${_perf_sources})
target_include_directories(g7_perf PUBLIC include)
target_link_libraries(g7_perf PUBLIC g7::runtime PRIVATE nlohmann_json::nlohmann_json)
g7_set_warnings(g7_perf)
set_target_properties(g7_perf PROPERTIES FOLDER "tools")

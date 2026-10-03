# The world file format .g7world (M5 split from world): reading, writing and checking worlds, the
# terrain block and the vob components it describes - without rendering, physics or EnTT, so tools
# (g7-cook) can read worlds without linking the engine. nlohmann-json stays private (ADR 0017).
# Its headers keep the path g7/world/ (namespace g7::world): the format is part of world's
# vocabulary; the split only separates what has to be linked.
include(FetchContent)

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann-json nicht gefunden - lade v3.12.0 per FetchContent")
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        SYSTEM)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

g7_add_module(world_format DEPS core asset PRIVATE_LIBS nlohmann_json::nlohmann_json)

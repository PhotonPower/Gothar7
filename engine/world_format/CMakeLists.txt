# The world file format .g7world (M5 split from world): reading, writing and checking worlds, the
# terrain block and the vob components it describes - without rendering, physics or EnTT, so tools
# (g7-cook) can read worlds without linking the engine. nlohmann-json stays private (ADR 0017).
# Its headers keep the path g7/world/ (namespace g7::world): the format is part of world's
# vocabulary; the split only separates what has to be linked.
include(FetchContent)

# The target comes from engine/asset (found or fetched there).
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

g7_add_module(world_format DEPS core asset PRIVATE_LIBS nlohmann_json::nlohmann_json)

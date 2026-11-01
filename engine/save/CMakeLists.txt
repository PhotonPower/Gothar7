# Save games (M15, docs/modules/save.md): the game state as plain data, the world delta against the .g7world
# file and the state as a JSON document (part B; the file container .g7save follows with ADR 0021).
# nlohmann-json stays private (found or fetched in engine/asset).
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

g7_add_module(save DEPS core world_format world script gameplay PRIVATE_LIBS nlohmann_json::nlohmann_json)

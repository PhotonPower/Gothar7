# Save games (M15, docs/modules/save.md): the game state as plain data and JSON, the world delta against the
# .g7world file, and the save file .g7save (ADR 0021: chunks with CRC-32, JSON packed with zstd).
# nlohmann-json and zstd stay private (found or fetched in engine/asset).
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
if(NOT TARGET zstd::libzstd)
    find_package(zstd CONFIG REQUIRED)
endif()

g7_add_module(save DEPS core world_format world script gameplay
              PRIVATE_LIBS nlohmann_json::nlohmann_json zstd::libzstd)

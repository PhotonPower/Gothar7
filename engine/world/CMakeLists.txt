# EnTT is a public dependency of world (ADR 0005: components and entt::entity appear in the API,
# the registry does not). nlohmann-json reads and writes .g7world and stays private (ADR 0017).
# The nodeps preset fetches both in the same versions.
include(FetchContent)

find_package(EnTT CONFIG QUIET)
if(NOT EnTT_FOUND)
    message(STATUS "EnTT nicht gefunden - lade EnTT v3.16.0 per FetchContent")
    FetchContent_Declare(EnTT
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v3.16.0
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(EnTT)
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann-json nicht gefunden - lade v3.12.0 per FetchContent")
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        SYSTEM)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

g7_add_module(world DEPS core asset render physics PUBLIC_LIBS EnTT::EnTT PRIVATE_LIBS nlohmann_json::nlohmann_json)

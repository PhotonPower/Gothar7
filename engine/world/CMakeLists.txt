# EnTT is a public dependency of world (ADR 0005: components and entt::entity appear in the API,
# the registry does not). The .g7world format itself (nlohmann-json, ADR 0017) lives in world_format.
# The nodeps preset fetches EnTT in the same version.
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

g7_add_module(world DEPS core asset render physics world_format PUBLIC_LIBS EnTT::EnTT)

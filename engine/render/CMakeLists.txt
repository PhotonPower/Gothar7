# glad loads the OpenGL functions (ADR 0003); private to render, the only module with GL calls.
# The nodeps preset has no glad (its generator needs Python): render then builds without the GL
# backend and Device::create() reports an error, so run the game with --no-render there.
find_package(glad CONFIG QUIET)
if(glad_FOUND)
    g7_add_module(render DEPS core platform asset PRIVATE_LIBS glad::glad)
else()
    message(WARNING "glad nicht gefunden - render ohne OpenGL-Backend (Spiel mit --no-render starten)")
    g7_add_module(render DEPS core platform asset)
    target_compile_definitions(g7_render PRIVATE G7_RENDER_NO_GL)
endif()

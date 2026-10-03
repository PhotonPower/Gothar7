g7_add_module(runtime DEPS core platform asset render audio physics animation world script ai gameplay ui save)

# Development builds find the repository's assets (assets/source, assets/cooked) without copying;
# [assets] dev_mounts in engine.toml switches it at runtime. Shipping builds turn the option off.
if(G7_DEV_ASSETS)
    target_compile_definitions(g7_runtime PRIVATE G7_DEV_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
                                                  G7_DEV_SCRIPT_ROOT="${PROJECT_SOURCE_DIR}/game/scripts")
endif()

# Core logic; everything except the runtime entry point is unit testable without the game.
add_library(sacore_core STATIC
    version.cpp version.h
    mod_manifest.cpp mod_manifest.h
    dependency_resolver.cpp dependency_resolver.h
    logger.cpp logger.h
    config.cpp config.h
    event_bus.cpp event_bus.h
    events.h
    build_info.cpp build_info.h
    mod_loader.cpp mod_loader.h
    mod_api.cpp mod_api.h
    patch_manager.cpp patch_manager.h
    game/module_image.cpp game/module_image.h
    game/adapter.cpp game/adapter.h
    game/game_layout.h
    game/game.cpp game/game.h
    engine/engine.cpp engine/engine.h
    stability/crash_report.cpp stability/crash_report.h
    stability/stability.cpp stability/stability.h
    stability/zip_writer.cpp stability/zip_writer.h
    testing/scenario.cpp testing/scenario.h
    saves/save_file.cpp saves/save_file.h
    saves/save_service.cpp saves/save_service.h
    content/content_registry.cpp content/content_registry.h
    content/content_service.cpp content/content_service.h
    testing/harness.cpp testing/harness.h
    ${PROJECT_SOURCE_DIR}/src/adapters/adapters.cpp ${PROJECT_SOURCE_DIR}/src/adapters/adapters.h)
sacore_target(sacore_core)
target_include_directories(sacore_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/adapters)
target_link_libraries(sacore_core PUBLIC tomlplusplus::tomlplusplus sacore_api minhook PRIVATE bcrypt ole32 psapi)

# SACore.asi: the core runtime loaded by the ASI loader.
configure_file(runtime/sacore.rc.in ${CMAKE_CURRENT_BINARY_DIR}/sacore.rc @ONLY)
add_library(SACore SHARED runtime/dllmain.cpp runtime/core_runtime.cpp runtime/core_runtime.h
    runtime/early_hooks.cpp runtime/early_hooks.h ${CMAKE_CURRENT_BINARY_DIR}/sacore.rc)
sacore_asi(SACore)
target_compile_definitions(SACore PRIVATE SACORE_VERSION_STRING="${SACORE_VERSION}")
target_link_libraries(SACore PRIVATE sacore_core)

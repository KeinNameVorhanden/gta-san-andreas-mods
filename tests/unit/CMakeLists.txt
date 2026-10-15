add_executable(sacore_unit_tests
    main.cpp
    version_test.cpp
    mod_manifest_test.cpp
    dependency_resolver_test.cpp
    config_test.cpp
    logger_test.cpp
    event_bus_test.cpp
    adapter_test.cpp
    crash_report_test.cpp
    scenario_test.cpp
    patch_manager_test.cpp
    save_file_test.cpp
    content_registry_test.cpp
    zip_writer_test.cpp
    build_info_test.cpp)
sacore_target(sacore_unit_tests)
target_link_libraries(sacore_unit_tests PRIVATE sacore_core doctest::doctest)
add_test(NAME sacore_unit_tests COMMAND sacore_unit_tests)

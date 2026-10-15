# Test mods exercising the mod loader at runtime (not shipped). Manifests live next to this file.
function(sacore_test_mod name behaviour)
    add_library(${name} SHARED test_mod.cpp)
    sacore_target(${name})
    target_compile_definitions(${name} PRIVATE TEST_MOD_BEHAVIOUR=${behaviour})
    target_link_libraries(${name} PRIVATE sacore_api)
endfunction()

sacore_test_mod(test_ok 0)
sacore_test_mod(test_fail 1)
sacore_test_mod(test_crash 2)
sacore_test_mod(test_needs_fail 0)

add_library(test_api SHARED test_api.cpp)
sacore_target(test_api)
target_link_libraries(test_api PRIVATE sacore_api)

add_library(test_event_crash SHARED test_api.cpp)
sacore_target(test_event_crash)
target_compile_definitions(test_event_crash PRIVATE TEST_API_CRASH_IN_EVENT=1)
target_link_libraries(test_event_crash PRIVATE sacore_api)

# Trip skip: built as a SACore mod DLL (C++20, x86).
add_library(trip_skip SHARED src/trip_skip.cpp mod.toml)
sacore_target(trip_skip)
target_link_libraries(trip_skip PRIVATE sacore_api)

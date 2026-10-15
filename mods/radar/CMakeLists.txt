# Radar zoom and GPS: built as a SACore mod DLL (C++20, x86).
add_library(radar SHARED src/radar.cpp mod.toml)
sacore_target(radar)
target_link_libraries(radar PRIVATE sacore_api)

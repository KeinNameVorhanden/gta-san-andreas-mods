# Developer console mod (optional, not required by any other mod).
add_library(devtools SHARED devtools.cpp mod.toml)
sacore_target(devtools)
target_link_libraries(devtools PRIVATE sacore_api imgui_dx9 sacore_gta_ui)

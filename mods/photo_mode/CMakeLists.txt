# HUD toggle and photo mode: built as a SACore mod DLL (C++20, x86).
add_library(photo_mode SHARED src/photo_mode.cpp mod.toml)
sacore_target(photo_mode)
target_link_libraries(photo_mode PRIVATE sacore_api)

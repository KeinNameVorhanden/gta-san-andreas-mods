# Widescreen FOV: built as a SACore mod DLL (C++20, x86).
add_library(widescreen SHARED src/widescreen.cpp mod.toml)
sacore_target(widescreen)
target_link_libraries(widescreen PRIVATE sacore_api)

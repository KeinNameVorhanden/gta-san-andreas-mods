# Load last save: built as a SACore mod DLL (C++20, x86).
add_library(load_last SHARED src/load_last.cpp mod.toml)
sacore_target(load_last)
target_link_libraries(load_last PRIVATE sacore_api)

# Autosave: built as a SACore mod DLL (C++20, x86).
add_library(autosave SHARED src/autosave.cpp mod.toml)
sacore_target(autosave)
target_link_libraries(autosave PRIVATE sacore_api)

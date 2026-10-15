# Quick save: built as a SACore mod DLL (C++20, x86).
add_library(quicksave SHARED src/quicksave.cpp mod.toml)
sacore_target(quicksave)
target_link_libraries(quicksave PRIVATE sacore_api)

# Cutscene skip: built as a SACore mod DLL (C++20, x86).
add_library(cutscene_skip SHARED src/cutscene_skip.cpp mod.toml)
sacore_target(cutscene_skip)
target_link_libraries(cutscene_skip PRIVATE sacore_api)

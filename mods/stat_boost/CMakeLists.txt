# Stat boost: built as a SACore mod DLL (C++20, x86).
add_library(stat_boost SHARED src/stat_boost.cpp mod.toml)
sacore_target(stat_boost)
target_link_libraries(stat_boost PRIVATE sacore_api)

# Session stats (SACore example): built as a SACore mod DLL (C++20, x86).
add_library(session_stats SHARED src/session_stats.cpp mod.toml)
sacore_target(session_stats)
target_link_libraries(session_stats PRIVATE sacore_api)

# Mission retry: built as a SACore mod DLL (C++20, x86).
add_library(mission_retry SHARED src/mission_retry.cpp mod.toml)
sacore_target(mission_retry)
target_link_libraries(mission_retry PRIVATE sacore_api)

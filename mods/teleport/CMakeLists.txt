# Waypoint teleport and fast travel: built as a SACore mod DLL (C++20, x86).
add_library(teleport SHARED src/teleport.cpp mod.toml)
sacore_target(teleport)
target_link_libraries(teleport PRIVATE sacore_api)

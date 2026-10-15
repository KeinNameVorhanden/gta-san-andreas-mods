# Police switches: built as a SACore mod DLL (C++20, x86).
add_library(police SHARED src/police.cpp mod.toml)
sacore_target(police)
target_link_libraries(police PRIVATE sacore_api)
